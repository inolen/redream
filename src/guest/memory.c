/*
 * dreamcast memory allocations and mappings
 *
 * responsible for allocating the host memory that backs the physical memory
 * available on the dreamcast, and providing access to the memory from both
 * the arm7 and sh4 address spaces
 *
 * the code generates a page table for each address space, where each page
 * is backed by a read and write callback that can handle each access
 *
 * if HAVE_FASTMEM is defined, the code will use mmap to create 32-bit address
 * spaces on the host machine that directly map to both the sh4 and arm7
 * address spaces. with this, physical memory can be directly accessed using
 * basic pointer arithmetic:
 * uint8_t data = *(base + 0x8c000000);
 *
 * note however, while this works for physical memory, this doesn't work for
 * mmio areas which must be accessed through a callback. due to this, each mmio
 * area is mprotect'd with all permissions disabled, which results in a segfault
 * if accessed. this mechanic is used by the jit to optimistically compile code
 * to go the fast route, falling back to calling into *_read_bytes or
 * *_write_bytes if a segfault occurs
 */

#include <stdint.h>
#include "guest/memory.h"
#include "core/core.h"
#include "guest/arm7/arm7.h"
#include "guest/dreamcast.h"
#include "guest/sh4/sh4.h"

/* physical memory constants */
#define RAM_SIZE 16 * 1024 * 1024
#define RAM_OFFSET 0
#define VRAM_SIZE 8 * 1024 * 1024
#define VRAM_OFFSET RAM_SIZE
#define ARAM_SIZE 2 * 1024 * 1024
#define ARAM_OFFSET VRAM_OFFSET + VRAM_SIZE
#define PHYSICAL_SIZE RAM_SIZE + VRAM_SIZE + ARAM_SIZE

/* page table constants */
#define MEM_PAGE_BITS 11
#define MEM_OFFSET_BITS 21
#define MEM_MAX_PAGES (1 << MEM_PAGE_BITS)
#define MEM_PAGE_SHIFT MEM_OFFSET_BITS
#define MEM_OFFSET_MASK ((1 << MEM_OFFSET_BITS) - 1)

/* address spaces provide different views of the same physical memory */
struct address_space {
  uint8_t *base;

  /* page table */
  uint8_t *ptrs[MEM_MAX_PAGES];
  mmio_read_cb read[MEM_MAX_PAGES];
  mmio_write_cb write[MEM_MAX_PAGES];
  mmio_read_string_cb read_string[MEM_MAX_PAGES];
  mmio_write_string_cb write_string[MEM_MAX_PAGES];
};

struct memory {
  struct dreamcast *dc;

#ifdef HAVE_FASTMEM
  /* shared memory object that backs the ram / vram / aram when using
     fastmem */
  shmem_handle_t shmem;
#endif

  /* the machine's physical memory */
  uint8_t *ram;
  uint8_t *vram;
  uint8_t *aram;

  /* each cpu has a different address space */
  struct address_space arm7;
  struct address_space sh4;
};

static int reserve_address_space(uint8_t **base) {
  /* find a contiguous 32-bit range of memory to map an address space to */
  const uint64_t ADDRESS_SPACE_SIZE = UINT64_C(1) << 32;
  int i = 64;

  while (i > 32) {
    i--;

    *base = (uint8_t *)(UINT64_C(1) << i);

    if (!reserve_pages(*base, ADDRESS_SPACE_SIZE)) {
      continue;
    }

    /* reservation was a success, release now so shared memory can be mapped
       into it */
    release_pages(*base, ADDRESS_SPACE_SIZE);

    return 1;
  }

  LOG_WARNING("failed to reserve address space");

  return 0;
}

static uint32_t mem_unhandled_read(struct memory *mem, uint32_t addr,
                                   uint32_t data_mask) {
  LOG_WARNING("mem_unhandled_read addr=0x%08x", addr);
  return 0;
}

static void mem_unhandled_write(struct memory *mem, uint32_t addr,
                                uint32_t data, uint32_t data_mask) {
  LOG_WARNING("mem_unhandled_write addr=0x%08x", addr);
}

/*
 * address space common
 */
enum {
  MAP_MMIO,
  MAP_RAM,
  MAP_VRAM,
  MAP_ARAM,
};

#define DEFINE_ADDRESS_SPACE(space)             \
  define_lookup_ex(space);                      \
  define_lookup(space);                         \
  define_memcpy(space);                         \
  define_memcpy_to_host(space);                 \
  define_memcpy_to_guest(space);                \
  define_write_bytes(space, write32, uint32_t); \
  define_write_bytes(space, write16, uint16_t); \
  define_write_bytes(space, write8, uint8_t);   \
  define_read_bytes(space, read32, uint32_t);   \
  define_read_bytes(space, read16, uint16_t);   \
  define_read_bytes(space, read8, uint8_t);     \
  define_base(space);

#define define_lookup_ex(space)                                               \
  static void space##_lookup_ex(                                              \
      struct memory *mem, uint32_t addr, void **userdata, uint8_t **ptr,      \
      mmio_read_cb *read, mmio_write_cb *write,                               \
      mmio_read_string_cb *read_string, mmio_write_string_cb *write_string) { \
    int page = addr >> MEM_PAGE_SHIFT;                                        \
    if (userdata) {                                                           \
      *userdata = mem->dc->space;                                             \
    }                                                                         \
    if (ptr && (*ptr = mem->space.ptrs[page])) {                              \
      *ptr += (addr & MEM_OFFSET_MASK);                                       \
    }                                                                         \
    if (read) {                                                               \
      *read = mem->space.read[page];                                          \
    }                                                                         \
    if (write) {                                                              \
      *write = mem->space.write[page];                                        \
    }                                                                         \
    if (read_string) {                                                        \
      *read_string = mem->space.read_string[page];                            \
    }                                                                         \
    if (write_string) {                                                       \
      *write_string = mem->space.write_string[page];                          \
    }                                                                         \
  }

#define define_lookup(space)                                              \
  void space##_lookup(struct memory *mem, uint32_t addr, void **userdata, \
                      uint8_t **ptr, mmio_read_cb *read,                  \
                      mmio_write_cb *write) {                             \
    space##_lookup_ex(mem, addr, userdata, ptr, read, write, NULL, NULL); \
  }

#define define_memcpy(space)                                                   \
  void space##_memcpy(struct memory *mem, uint32_t dst, uint32_t src,          \
                      int size) {                                              \
    uint8_t *pdst = NULL;                                                      \
    mmio_write_cb write = NULL;                                                \
    mmio_write_string_cb write_string = NULL;                                  \
    space##_lookup_ex(mem, dst, NULL, &pdst, NULL, &write, NULL,               \
                      &write_string);                                          \
                                                                               \
    uint8_t *psrc = NULL;                                                      \
    mmio_read_cb read = NULL;                                                  \
    mmio_read_string_cb read_string = NULL;                                    \
    space##_lookup_ex(mem, src, NULL, &psrc, &read, NULL, &read_string, NULL); \
                                                                               \
    if (pdst && psrc) {                                                        \
      memcpy(pdst, psrc, size);                                                \
    } else if (pdst && read_string) {                                          \
      read_string(mem->dc->space, pdst, src, size);                            \
    } else if (psrc && write_string) {                                         \
      write_string(mem->dc->space, dst, psrc, size);                           \
    } else if (pdst) {                                                         \
      uint32_t end = src + size;                                               \
      while (src < end) {                                                      \
        *pdst = read(mem->dc->space, src, 0xff);                               \
        pdst++;                                                                \
        src++;                                                                 \
      }                                                                        \
    } else if (psrc) {                                                         \
      uint32_t end = dst + size;                                               \
      while (dst < end) {                                                      \
        write(mem->dc->space, dst, *psrc, 0xff);                               \
        psrc++;                                                                \
        dst++;                                                                 \
      }                                                                        \
    } else {                                                                   \
      uint32_t end = src + size;                                               \
      while (src < end) {                                                      \
        uint8_t data = read(mem->dc->space, src, 0xff);                        \
        write(mem->dc->space, dst, data, 0xff);                                \
        src++;                                                                 \
        dst++;                                                                 \
      }                                                                        \
    }                                                                          \
  }

#define define_memcpy_to_host(space)                                           \
  void space##_memcpy_to_host(struct memory *mem, void *ptr, uint32_t src,     \
                              int size) {                                      \
    uint8_t *pdst = ptr;                                                       \
    uint8_t *psrc = NULL;                                                      \
    mmio_read_cb read = NULL;                                                  \
    mmio_read_string_cb read_string = NULL;                                    \
    space##_lookup_ex(mem, src, NULL, &psrc, &read, NULL, &read_string, NULL); \
                                                                               \
    if (psrc) {                                                                \
      memcpy(pdst, psrc, size);                                                \
    } else if (read_string) {                                                  \
      read_string(mem->dc->space, pdst, src, size);                            \
    } else {                                                                   \
      uint32_t end = src + size;                                               \
      while (src < end) {                                                      \
        *pdst = read(mem->dc->space, src, 0xff);                               \
        pdst++;                                                                \
        src++;                                                                 \
      }                                                                        \
    }                                                                          \
  }

#define define_memcpy_to_guest(space)                            \
  void space##_memcpy_to_guest(struct memory *mem, uint32_t dst, \
                               const void *ptr, int size) {      \
    const uint8_t *psrc = ptr;                                   \
    uint8_t *pdst = NULL;                                        \
    mmio_write_cb write = NULL;                                  \
    mmio_write_string_cb write_string = NULL;                    \
    space##_lookup_ex(mem, dst, NULL, &pdst, NULL, &write, NULL, \
                      &write_string);                            \
                                                                 \
    if (pdst) {                                                  \
      memcpy(pdst, psrc, size);                                  \
    } else if (write_string) {                                   \
      write_string(mem->dc->space, dst, psrc, size);             \
    } else {                                                     \
      uint32_t end = dst + size;                                 \
      while (dst < end) {                                        \
        write(mem->dc->space, dst, *psrc, 0xff);                 \
        psrc++;                                                  \
        dst++;                                                   \
      }                                                          \
    }                                                            \
  }

#define define_write_bytes(space, name, data_type)                           \
  void space##_##name(struct memory *mem, uint32_t addr, data_type data) {   \
    int page = addr >> MEM_PAGE_SHIFT;                                       \
    uint8_t *ptr = mem->space.ptrs[page];                                    \
    if (ptr) {                                                               \
      addr &= MEM_OFFSET_MASK;                                               \
      *(data_type *)(ptr + addr) = data;                                     \
      return;                                                                \
    }                                                                        \
    const uint32_t data_mask = (UINT64_C(1) << (sizeof(data_type) * 8)) - 1; \
    mmio_write_cb write = mem->space.write[page];                            \
    write(mem->dc->space, addr, data, data_mask);                            \
  }

#define define_read_bytes(space, name, data_type)                            \
  data_type space##_##name(struct memory *mem, uint32_t addr) {              \
    int page = addr >> MEM_PAGE_SHIFT;                                       \
    uint8_t *ptr = mem->space.ptrs[page];                                    \
    if (ptr) {                                                               \
      addr &= MEM_OFFSET_MASK;                                               \
      return *(data_type *)(ptr + addr);                                     \
    }                                                                        \
    const uint32_t data_mask = (UINT64_C(1) << (sizeof(data_type) * 8)) - 1; \
    mmio_read_cb read = mem->space.read[page];                               \
    return read(mem->dc->space, addr, data_mask);                            \
  }

#define define_base(space)                    \
  uint8_t *space##_base(struct memory *mem) { \
    return mem->space.base;                   \
  }

static void as_map(struct memory *mem, struct address_space *space,
                   uint32_t begin, uint32_t size, int type, mmio_read_cb read,
                   mmio_write_cb write, mmio_read_string_cb read_string,
                   mmio_write_string_cb write_string) {
  uint32_t end = begin - 1 + size;
  uint32_t page_size = 1 << MEM_PAGE_SHIFT;

  int offset = -1;
  uint8_t *ptr = NULL;

  switch (type) {
    case MAP_RAM:
      offset = RAM_OFFSET;
      ptr = mem->ram;
      break;
    case MAP_VRAM:
      offset = VRAM_OFFSET;
      ptr = mem->vram;
      break;
    case MAP_ARAM:
      offset = ARAM_OFFSET;
      ptr = mem->aram;
      break;
  }

  /* add entries to page table */
  CHECK(size % page_size == 0);

  for (uint32_t page_offset = 0; page_offset < size; page_offset += page_size) {
    uint32_t addr = begin + page_offset;
    int page = addr >> MEM_PAGE_SHIFT;

    if (ptr) {
      space->ptrs[page] = ptr + page_offset;
      space->read[page] = NULL;
      space->write[page] = NULL;
      space->read_string[page] = NULL;
      space->write_string[page] = NULL;
    } else {
      space->ptrs[page] = NULL;
      space->read[page] = read;
      space->write[page] = write;
      space->read_string[page] = read_string;
      space->write_string[page] = write_string;
    }
  }

#ifdef HAVE_FASTMEM
  uint8_t *target = space->base + begin;
  void *res = NULL;

  if (offset >= 0) {
    /* map physical memory into the address space */
    res = map_shared_memory(mem->shmem, offset, target, size, ACC_READWRITE);
  } else {
    /* disable access to mmio areas */
    res = map_shared_memory(mem->shmem, 0x0, target, size, ACC_NONE);
  }

  CHECK_NE(res, SHMEM_MAP_FAILED);
#else
  (void)(offset);
#endif
}

static int as_init(struct address_space *space) {
  /* bind default handler */
  for (int i = 0; i < MEM_MAX_PAGES; i++) {
    space->read[i] = (mmio_read_cb)&mem_unhandled_read;
    space->write[i] = (mmio_write_cb)&mem_unhandled_write;
  }

#ifdef HAVE_FASTMEM
  if (!reserve_address_space(&space->base)) {
    return 0;
  }
#endif

  return 1;
}

/*
 * arm7 address space
 */
DEFINE_ADDRESS_SPACE(arm7);

int arm7_init(struct memory *mem) {
  struct address_space *space = &mem->arm7;

  if (!as_init(space)) {
    return 0;
  }

  uint32_t ARM7_AICA_MEM_SIZE = ARM7_AICA_MEM_END - ARM7_AICA_MEM_BEGIN + 1;
  uint32_t ARM7_AICA_REG_SIZE = ARM7_AICA_REG_END - ARM7_AICA_REG_BEGIN + 1;

  as_map(mem, space, ARM7_AICA_MEM_BEGIN, ARM7_AICA_MEM_SIZE, MAP_ARAM, NULL,
         NULL, NULL, NULL);
  as_map(mem, space, ARM7_AICA_REG_BEGIN, ARM7_AICA_MEM_SIZE, MAP_MMIO,
         (mmio_read_cb)&arm7_mem_read, (mmio_write_cb)&arm7_mem_write, NULL,
         NULL);

  return 1;
}

/*
 * sh4 address space
 */
DEFINE_ADDRESS_SPACE(sh4);

/* physical memory mirrors */
enum {
  P0 = 0x01,
  P1 = 0x02,
  P2 = 0x04,
  P3 = 0x08,
  P4 = 0x10,
};

/* helper to map each physical region into its logical mirrors (P0-P4) */
static void sh4_map(struct memory *mem, uint32_t begin, uint32_t end,
                    int regions, int type, mmio_read_cb read,
                    mmio_write_cb write, mmio_read_string_cb read_string,
                    mmio_write_string_cb write_string) {
  struct address_space *space = &mem->sh4;
  uint32_t size = end - begin + 1;

  if (regions & P0) {
    as_map(mem, space, SH4_P0_00_BEGIN | begin, size, type, read, write,
           read_string, write_string);
    as_map(mem, space, SH4_P0_01_BEGIN | begin, size, type, read, write,
           read_string, write_string);
    as_map(mem, space, SH4_P0_10_BEGIN | begin, size, type, read, write,
           read_string, write_string);
    as_map(mem, space, SH4_P0_11_BEGIN | begin, size, type, read, write,
           read_string, write_string);
  }

  if (regions & P1) {
    as_map(mem, space, SH4_P1_BEGIN | begin, size, type, read, write,
           read_string, write_string);
  }

  if (regions & P2) {
    as_map(mem, space, SH4_P2_BEGIN | begin, size, type, read, write,
           read_string, write_string);
  }

  if (regions & P3) {
    as_map(mem, space, SH4_P3_BEGIN | begin, size, type, read, write,
           read_string, write_string);
  }

  if (regions & P4) {
    as_map(mem, space, SH4_P4_BEGIN | begin, size, type, read, write,
           read_string, write_string);
  }
}

int sh4_init(struct memory *mem) {
  struct address_space *space = &mem->sh4;

  if (!as_init(space)) {
    return 0;
  }

  /* note, p0-p3 map to the entire external address space, while p4 only maps to
     the external regions in between the gaps in its own internal regions. these
     gaps map to areas 1-3 (0xe4000000-0xefffffff) and 6-7 (0xf8000000-
     0xffffffff) */

  /* area 0 */
  sh4_map(mem, SH4_AREA0_BEGIN, SH4_AICA_MEM_BEGIN - 1, P0 | P1 | P2 | P3,
          MAP_MMIO, (mmio_read_cb)&sh4_area0_read,
          (mmio_write_cb)&sh4_area0_write, NULL, NULL);
  sh4_map(mem, SH4_AICA_MEM_BEGIN, SH4_AICA_MEM_END, P0 | P1 | P2 | P3,
          MAP_ARAM, NULL, NULL, NULL, NULL);
  sh4_map(mem, SH4_AICA_MEM_END + 1, SH4_AREA0_END, P0 | P1 | P2 | P3, MAP_MMIO,
          (mmio_read_cb)&sh4_area0_read, (mmio_write_cb)&sh4_area0_write, NULL,
          NULL);

  /* area 1 */
  sh4_map(mem, SH4_AREA1_BEGIN, SH4_AREA1_END, P0 | P1 | P2 | P3 | P4, MAP_MMIO,
          (mmio_read_cb)&sh4_area1_read, (mmio_write_cb)&sh4_area1_write, NULL,
          NULL);
#if 0
  /* TODO make texture watches monitor all mirrors such that the 64-bit access
     area can be directly mapped, no callback */
  sh4_map(mem, 0x04000000, 0x047fffff, P0 | P1 | P2 | P3, MAP_VRAM, NULL,
          NULL, NULL, NULL);
  sh4_map(mem, 0x06000000, 0x067fffff, P0 | P1 | P2 | P3, MAP_VRAM, NULL,
          NULL, NULL, NULL);
#endif

  /* area 2 */

  /* area 3 */
  sh4_map(mem, SH4_AREA3_RAM0_BEGIN, SH4_AREA3_RAM0_END, P0 | P1 | P2 | P3 | P4,
          MAP_RAM, NULL, NULL, NULL, NULL);
  sh4_map(mem, SH4_AREA3_RAM1_BEGIN, SH4_AREA3_RAM1_END, P0 | P1 | P2 | P3 | P4,
          MAP_RAM, NULL, NULL, NULL, NULL);
  sh4_map(mem, SH4_AREA3_RAM2_BEGIN, SH4_AREA3_RAM2_END, P0 | P1 | P2 | P3 | P4,
          MAP_RAM, NULL, NULL, NULL, NULL);
  sh4_map(mem, SH4_AREA3_RAM3_BEGIN, SH4_AREA3_RAM3_END, P0 | P1 | P2 | P3 | P4,
          MAP_RAM, NULL, NULL, NULL, NULL);

  /* area 4. this region is only written through sq / dma transfers, so only a
     write_string handler is added */
  sh4_map(mem, SH4_AREA4_BEGIN, SH4_AREA4_END, P0 | P1 | P2 | P3, MAP_MMIO,
          (mmio_read_cb)&sh4_area4_read, NULL, NULL,
          (mmio_write_string_cb)&sh4_area4_write);

  /* area 5 */

  /* area 6 */

  /* area 7 */
  sh4_map(mem, SH4_AREA7_BEGIN, SH4_AREA7_END, P0 | P1 | P2 | P3 | P4, MAP_MMIO,
          (mmio_read_cb)&sh4_area7_read, (mmio_write_cb)&sh4_area7_write, NULL,
          NULL);

  /* p4. the unassigned regions have already been mapped to the external address
     space. instead of mapping the entire p4 area, selectively map each internal
     region to avoid overwriting the existing mappings */
  sh4_map(mem, SH4_SQ_BEGIN, SH4_SQ_END, P4, MAP_MMIO,
          (mmio_read_cb)&sh4_p4_read, (mmio_write_cb)&sh4_p4_write, NULL, NULL);
  sh4_map(mem, SH4_ICACHE_BEGIN, SH4_ICACHE_END, P4, MAP_MMIO,
          (mmio_read_cb)&sh4_p4_read, (mmio_write_cb)&sh4_p4_write, NULL, NULL);
  sh4_map(mem, SH4_ITLB_BEGIN, SH4_ITLB_END, P4, MAP_MMIO,
          (mmio_read_cb)&sh4_p4_read, (mmio_write_cb)&sh4_p4_write, NULL, NULL);
  sh4_map(mem, SH4_OCACHE_BEGIN, SH4_OCACHE_END, P4, MAP_MMIO,
          (mmio_read_cb)&sh4_p4_read, (mmio_write_cb)&sh4_p4_write, NULL, NULL);
  sh4_map(mem, SH4_UTLB_BEGIN, SH4_UTLB_END, P4, MAP_MMIO,
          (mmio_read_cb)&sh4_p4_read, (mmio_write_cb)&sh4_p4_write, NULL, NULL);

  return 1;
}

uint8_t *mem_vram(struct memory *mem, uint32_t offset) {
  return mem->vram + offset;
}

uint8_t *mem_aram(struct memory *mem, uint32_t offset) {
  return mem->aram + offset;
}

uint8_t *mem_ram(struct memory *mem, uint32_t offset) {
  return mem->ram + offset;
}

int mem_init(struct memory *mem) {
#ifdef HAVE_FASTMEM
  /* create the shared memory object to back the physical memory. note, because
     mmio regions also map this shared memory object when disabling permissions,
     the object has to at least be the size of an entire mmio region */
  size_t shmem_size = MAX(PHYSICAL_SIZE, SH4_AREA_SIZE);
  mem->shmem = create_shared_memory("/redream", shmem_size, ACC_READWRITE);

  if (mem->shmem == SHMEM_INVALID) {
    LOG_WARNING("mem_init failed to create shared memory object");
    return 0;
  }

  mem->ram =
      map_shared_memory(mem->shmem, RAM_OFFSET, NULL, RAM_SIZE, ACC_READWRITE);
  CHECK_NE(mem->ram, SHMEM_MAP_FAILED);

  mem->vram = map_shared_memory(mem->shmem, VRAM_OFFSET, NULL, VRAM_SIZE,
                                ACC_READWRITE);
  CHECK_NE(mem->vram, SHMEM_MAP_FAILED);

  mem->aram = map_shared_memory(mem->shmem, ARAM_OFFSET, NULL, ARAM_SIZE,
                                ACC_READWRITE);
  CHECK_NE(mem->aram, SHMEM_MAP_FAILED);
#else
  mem->ram = calloc(RAM_SIZE, 1);
  mem->vram = calloc(VRAM_SIZE, 1);
  mem->aram = calloc(ARAM_SIZE, 1);
#endif

  if (!sh4_init(mem)) {
    return 0;
  }

  if (!arm7_init(mem)) {
    return 0;
  }

  return 1;
}

void mem_destroy(struct memory *mem) {
#ifdef HAVE_FASTMEM
  destroy_shared_memory(mem->shmem);
#else
  free(mem->ram);
  free(mem->vram);
  free(mem->aram);
#endif

  free(mem);
}

struct memory *mem_create(struct dreamcast *dc) {
  struct memory *mem = calloc(1, sizeof(struct memory));

  mem->dc = dc;

#ifdef HAVE_FASTMEM
  mem->shmem = SHMEM_INVALID;
#endif

  return mem;
}
