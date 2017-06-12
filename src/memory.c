#include "memory.h"
#include "core/exception_handler.h"
#include "core/math.h"
#include "core/string.h"
#include "dreamcast.h"

/*
 * address maps
 */
#define MAX_MAP_ENTRIES 1024

enum map_entry_type {
  MAP_ENTRY_PHYSICAL,
  MAP_ENTRY_MMIO,
  MAP_ENTRY_DEVICE,
  MAP_ENTRY_MIRROR,
};

struct address_map_entry {
  enum map_entry_type type;

  uint32_t size;
  uint32_t addr;
  uint32_t addr_mask;

  union {
    struct {
      struct memory_region *region;
    } physical;

    struct {
      struct memory_region *region;
    } mmio;

    struct {
      void *device;
      address_map_cb mapper;
    } device;

    struct {
      uint32_t physical_addr;
    } mirror;
  };
};

struct address_map {
  struct address_map_entry entries[MAX_MAP_ENTRIES];
  int num_entries;
};

/*
 * address spaces
 */

/* helpers for extracting page information out of a virtual address */
#define PAGE_BITS 20
#define PAGE_OFFSET_BITS (32 - PAGE_BITS)
#define PAGE_SIZE (1 << PAGE_OFFSET_BITS)
#define PAGE_OFFSET_MASK (uint32_t)(PAGE_SIZE - 1)
#define PAGE_INDEX_MASK (uint32_t)(~PAGE_OFFSET_MASK)
#define NUM_PAGES (1 << PAGE_BITS)

/* helpers for region information out of a page table entry */
#define REGION_HANDLE_MASK (page_entry_t)(MAX_REGIONS - 1)
#define REGION_OFFSET_MASK (page_entry_t)(~REGION_HANDLE_MASK)
#define MAX_REGIONS (1 << PAGE_OFFSET_BITS)

typedef uint32_t page_entry_t;

struct address_space {
  struct dreamcast *dc;
  page_entry_t pages[NUM_PAGES];
  uint8_t *base;
};

/*
 * memory
 a*/
#define ADDRESS_SPACE_SIZE (UINT64_C(1) << 32)

enum region_type {
  REGION_PHYSICAL,
  REGION_MMIO,
};

struct memory_region {
  enum region_type type;

  int handle;
  const char *name;
  uint32_t size;

  union {
    struct {
      uint32_t shmem_offset;
    } physical;

    struct {
      void *data;
      mmio_read_cb read;
      mmio_write_cb write;
      mmio_read_string_cb read_string;
      mmio_write_string_cb write_string;
    } mmio;
  };
};

struct memory {
  struct dreamcast *dc;

  shmem_handle_t shmem;
  uint32_t shmem_size;
  uint8_t *shmem_base;

  struct memory_region regions[MAX_REGIONS];
  int num_regions;
};

static inline int is_page_aligned(uint32_t start, uint32_t size) {
  return (start & ((1 << PAGE_OFFSET_BITS) - 1)) == 0 &&
         ((start + size) & ((1 << PAGE_OFFSET_BITS) - 1)) == 0;
}

static inline uint32_t get_total_page_size(int num_pages) {
  return (uint32_t)num_pages * PAGE_SIZE;
}

/* map virtual addresses to pages */
static inline int get_page_index(uint32_t addr) {
  return addr >> PAGE_OFFSET_BITS;
}

static inline uint32_t get_page_offset(uint32_t addr) {
  return addr & PAGE_OFFSET_MASK;
}

/* pack and unpack page entry bitstrings */
static inline page_entry_t pack_page_entry(int region_handle,
                                           uint32_t region_offset) {
  return region_offset | region_handle;
}

static inline int get_region_offset(page_entry_t page) {
  return page & REGION_OFFSET_MASK;
}

static inline int get_region_handle(page_entry_t page) {
  return page & REGION_HANDLE_MASK;
}

/* iterate mirrors for a given address and mask */
struct mirror_iterator {
  uint32_t base, mask, imask, step;
  uint32_t i, addr;
  int first;
};

static void mirror_iterator_init(struct mirror_iterator *it, uint32_t addr,
                                 uint32_t mask) {
  it->base = addr & mask;
  it->mask = mask;
  it->imask = ~mask;
  it->step = 1 << ctz32(it->imask);
  it->i = 0;
  it->addr = it->base;
  it->first = 1;
}

static int mirror_iterator_next(struct mirror_iterator *it) {
  /* first iteration just returns base */
  if (it->first) {
    it->first = 0;
    return 1;
  }

  /* stop once mask complement is completely set */
  if ((it->addr & it->imask) == it->imask) {
    return 0;
  }

  /* step to the next permutation */
  it->i += it->step;

  /* if the new value carries over into a masked off bit, skip it */
  uint32_t carry;
  do {
    carry = it->i & it->mask;
    it->i += carry;
  } while (carry);

  /* merge with the base */
  it->addr = it->base | it->i;

  return 1;
}

static int reserve_address_space(uint8_t **base) {
  /* find a contiguous (1 << 32) chunk of memory to map an address space to */
  int i = 64;

  while (i > 32) {
    i--;

    *base = (uint8_t *)(1ull << i);

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

static uint32_t default_mmio_read(void *userdata, uint32_t addr,
                                  uint32_t data_mask) {
  LOG_WARNING("unexpected read from 0x%08x", addr);
  return 0;
}

static void default_mmio_write(void *userdata, uint32_t addr, uint32_t data,
                               uint32_t data_mask) {
  LOG_WARNING("unexpected write to 0x%08x", addr);
}

static void default_mmio_read_string(void *userdata, void *ptr, uint32_t src,
                                     int size) {
  LOG_WARNING("unexpected string read from 0x%08x", src);
}

static void default_mmio_write_string(void *userdata, uint32_t dst,
                                      const void *ptr, int size) {
  LOG_WARNING("unexpected string write to 0x%08x", dst);
  exit(1);
}

struct memory_region *memory_get_region(struct memory *memory,
                                        const char *name) {
  for (int i = 1; i < memory->num_regions; i++) {
    struct memory_region *region = &memory->regions[i];

    if (!strcmp(region->name, name)) {
      return region;
    }
  }

  return NULL;
}

struct memory_region *memory_create_physical_region(struct memory *memory,
                                                    const char *name,
                                                    uint32_t size) {
  struct memory_region *region = memory_get_region(memory, name);

  if (!region) {
    CHECK_LT(memory->num_regions, MAX_REGIONS);

    region = &memory->regions[memory->num_regions];
    region->type = REGION_PHYSICAL;
    region->handle = memory->num_regions++;
    region->name = name;
    region->size = size;
    region->physical.shmem_offset = memory->shmem_size;
    memory->shmem_size += size;

    /* ensure physical memory regions are aligned to the allocation granularity,
       otherwise it will confusingly fail to map further down the line */
    size_t granularity = get_allocation_granularity();
    CHECK((memory->shmem_size & (granularity - 1)) == 0 &&
          ((memory->shmem_size + size) & (granularity - 1)) == 0);
  }

  return region;
}

struct memory_region *memory_create_mmio_region(
    struct memory *memory, const char *name, uint32_t size, void *data,
    mmio_read_cb read, mmio_write_cb write, mmio_read_string_cb read_string,
    mmio_write_string_cb write_string) {
  struct memory_region *region = memory_get_region(memory, name);

  if (!region) {
    CHECK_LT(memory->num_regions, MAX_REGIONS);

    region = &memory->regions[memory->num_regions];
    region->type = REGION_MMIO;
    region->handle = memory->num_regions++;
    region->name = name;
    region->size = size;
    region->mmio.data = data;

    /* bind default handlers if a valid one isn't specified */
    region->mmio.read = read ? read : &default_mmio_read;
    region->mmio.write = write ? write : &default_mmio_write;
    region->mmio.read_string =
        read_string ? read_string : &default_mmio_read_string;
    region->mmio.write_string =
        write_string ? write_string : &default_mmio_write_string;
  }

  return region;
}

uint8_t *memory_translate(struct memory *memory, const char *name,
                          uint32_t offset) {
  struct memory_region *region = memory_get_region(memory, name);
  CHECK_NOTNULL(region);
  return memory->shmem_base + region->physical.shmem_offset + offset;
}

static int memory_create_shmem(struct memory *memory) {
  /* create the shared memory object to back the address space */
  memory->shmem =
      create_shared_memory("/redream", ADDRESS_SPACE_SIZE, ACC_READWRITE);

  if (memory->shmem == SHMEM_INVALID) {
    LOG_WARNING("failed to create shared memory object");
    return 0;
  }

  return 1;
}

static void memory_destroy_shmem(struct memory *memory) {
  CHECK(unmap_shared_memory(memory->shmem, memory->shmem_base,
                            memory->shmem_size));
  destroy_shared_memory(memory->shmem);
}

int memory_init(struct memory *memory) {
  if (!memory_create_shmem(memory)) {
    return 0;
  }

  /* map each memory interface's address space */
  list_for_each_entry(dev, &memory->dc->devices, struct device, it) {
    if (dev->memory_if) {
      /* create the actual address map */
      struct address_map map = {0};
      dev->memory_if->mapper(dev, memory->dc, &map);

      /* apply the map to create the address space */
      CHECK(as_map(dev->memory_if->space, dev->name, &map));
    }
  }

  /* map raw address space */
  if (!reserve_address_space(&memory->shmem_base)) {
    return 0;
  }

  if (!map_shared_memory(memory->shmem, 0, memory->shmem_base,
                         memory->shmem_size, ACC_READWRITE)) {
    return 0;
  }

  return 1;
}

void memory_destroy(struct memory *memory) {
  memory_destroy_shmem(memory);
  free(memory);
}

struct memory *memory_create(struct dreamcast *dc) {
  struct memory *memory = calloc(1, sizeof(struct memory));

  memory->dc = dc;
  memory->shmem = SHMEM_INVALID;
  /* 0 page is reserved, meaning all valid page entries must be non-zero */
  memory->num_regions = 1;

  return memory;
}

static struct address_map_entry *address_map_alloc_entry(
    struct address_map *am) {
  CHECK_LT(am->num_entries, MAX_MAP_ENTRIES);
  struct address_map_entry *entry = &am->entries[am->num_entries++];
  memset(entry, 0, sizeof(*entry));
  return entry;
}

void am_mirror(struct address_map *am, uint32_t physical_addr, uint32_t size,
               uint32_t addr) {
  struct address_map_entry *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_MIRROR;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = 0xffffffff;
  entry->mirror.physical_addr = physical_addr;
}

void am_device(struct address_map *am, void *device, address_map_cb mapper,
               uint32_t size, uint32_t addr, uint32_t addr_mask) {
  struct address_map_entry *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_DEVICE;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = addr_mask;
  entry->device.device = device;
  entry->device.mapper = mapper;
}

void am_mmio(struct address_map *am, struct memory_region *region,
             uint32_t size, uint32_t addr, uint32_t addr_mask) {
  struct address_map_entry *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_MMIO;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = addr_mask;
  entry->mmio.region = region;
}

void am_physical(struct address_map *am, struct memory_region *region,
                 uint32_t size, uint32_t addr, uint32_t addr_mask) {
  struct address_map_entry *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_PHYSICAL;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = addr_mask;
  entry->physical.region = region;
}

static void as_lookup_region(struct address_space *space, uint32_t addr,

                             struct memory_region **region, uint32_t *offset) {
  page_entry_t page = space->pages[get_page_index(addr)];
  DCHECK(page);
  int region_handle = get_region_handle(page);
  *region = &space->dc->memory->regions[region_handle];
  *offset = get_region_offset(page) + get_page_offset(addr);
}

void as_memcpy(struct address_space *space, uint32_t dst, uint32_t src,
               int size) {
  struct memory_region *dst_region;
  uint32_t dst_offset;
  as_lookup_region(space, dst, &dst_region, &dst_offset);

  struct memory_region *src_region;
  uint32_t src_offset;
  as_lookup_region(space, src, &src_region, &src_offset);

  if (dst_region->type == REGION_PHYSICAL &&
      src_region->type == REGION_PHYSICAL) {
    memcpy(space->base + dst, space->base + src, size);
  } else if (dst_region->type == REGION_PHYSICAL) {
    src_region->mmio.read_string(src_region->mmio.data, space->base + dst,
                                 src_offset, size);
  } else if (src_region->type == REGION_PHYSICAL) {
    dst_region->mmio.write_string(dst_region->mmio.data, dst_offset,
                                  space->base + src, size);
  } else {
    /* the case where both regions are MMIO and both support read_string /
       write_string could be further optimized with a fixed buffer, but it
       currently never occurs */
    CHECK(
        "as_memcpy doesn't currently support copying between two MMIO regions");
  }
}

void as_memcpy_to_host(struct address_space *space, void *ptr, uint32_t src,
                       int size) {
  struct memory_region *src_region;
  uint32_t src_offset;
  as_lookup_region(space, src, &src_region, &src_offset);

  /* optimize copy under the assumption that the data being copied doesn't
     cross multiple regions */
  if (src_region->type == REGION_PHYSICAL) {
    memcpy(ptr, space->base + src, size);
  } else {
    src_region->mmio.read_string(src_region->mmio.data, ptr, src_offset, size);
  }
}

void as_memcpy_to_guest(struct address_space *space, uint32_t dst,
                        const void *ptr, int size) {
  struct memory_region *dst_region;
  uint32_t dst_offset;
  as_lookup_region(space, dst, &dst_region, &dst_offset);

  /* optimize copy under the assumption that the data being copied doesn't
     cross multiple regions */
  if (dst_region->type == REGION_PHYSICAL) {
    memcpy(space->base + dst, ptr, size);
  } else {
    dst_region->mmio.write_string(dst_region->mmio.data, dst_offset, ptr, size);
  }
}

#define define_read_bytes(name, data_type)                                     \
  data_type as_##name(struct address_space *space, uint32_t addr) {            \
    page_entry_t page = space->pages[get_page_index(addr)];                    \
    DCHECK(page);                                                              \
    int region_handle = get_region_handle(page);                               \
    struct memory_region *region = &space->dc->memory->regions[region_handle]; \
    if (region->type == REGION_PHYSICAL) {                                     \
      return *(data_type *)(space->base + addr);                               \
    }                                                                          \
    static const uint32_t data_mask = (1ull << (sizeof(data_type) * 8)) - 1;   \
    uint32_t region_offset = get_region_offset(page);                          \
    uint32_t page_offset = get_page_offset(addr);                              \
    return region->mmio.read(region->mmio.data, region_offset + page_offset,   \
                             data_mask);                                       \
  }

define_read_bytes(read8, uint8_t);
define_read_bytes(read16, uint16_t);
define_read_bytes(read32, uint32_t);

#define define_write_bytes(name, data_type)                                    \
  void as_##name(struct address_space *space, uint32_t addr, data_type data) { \
    page_entry_t page = space->pages[get_page_index(addr)];                    \
    DCHECK(page);                                                              \
    int region_handle = get_region_handle(page);                               \
    struct memory_region *region = &space->dc->memory->regions[region_handle]; \
    if (region->type == REGION_PHYSICAL) {                                     \
      *(data_type *)(space->base + addr) = data;                               \
      return;                                                                  \
    }                                                                          \
    static const uint32_t data_mask = (1ull << (sizeof(data_type) * 8)) - 1;   \
    uint32_t region_offset = get_region_offset(page);                          \
    uint32_t page_offset = get_page_offset(addr);                              \
    region->mmio.write(region->mmio.data, region_offset + page_offset, data,   \
                       data_mask);                                             \
  }

define_write_bytes(write8, uint8_t);
define_write_bytes(write16, uint16_t);
define_write_bytes(write32, uint32_t);

uint8_t *as_translate(struct address_space *space, uint32_t addr) {
  return space->base + addr;
}

void as_lookup(struct address_space *space, uint32_t addr, void **ptr,
               void **userdata, mmio_read_cb *read, mmio_write_cb *write,
               uint32_t *offset) {
  struct memory_region *region;
  uint32_t mmio_offset;
  as_lookup_region(space, addr, &region, &mmio_offset);

  if (region->type == REGION_PHYSICAL) {
    if (ptr) {
      *ptr = space->base + addr;
    }
    if (userdata) {
      *userdata = NULL;
    }
    if (read) {
      *read = NULL;
    }
    if (write) {
      *write = NULL;
    }
    if (offset) {
      *offset = 0;
    }
  } else {
    if (ptr) {
      *ptr = NULL;
    }
    if (userdata) {
      *userdata = region->mmio.data;
    }
    if (read) {
      *read = region->mmio.read;
    }
    if (write) {
      *write = region->mmio.write;
    }
    if (offset) {
      *offset = mmio_offset;
    }
  }
}

static void as_merge_map(struct address_space *space,
                         const struct address_map *map, uint32_t offset) {
  /* iterate regions in the supplied memory map in the other added, flattening
     them out into a virtual page table */
  for (int i = 0, n = map->num_entries; i < n; i++) {
    const struct address_map_entry *entry = &map->entries[i];

    /* iterate each mirror of the entry */
    struct mirror_iterator it = {0};

    mirror_iterator_init(&it, offset + entry->addr, entry->addr_mask);

    while (mirror_iterator_next(&it)) {
      uint32_t addr = it.addr;
      uint32_t size = entry->size;
      CHECK(is_page_aligned(addr, size));

      int first_page = get_page_index(addr);
      int num_pages = size >> PAGE_OFFSET_BITS;

      switch (entry->type) {
        case MAP_ENTRY_PHYSICAL: {
          struct memory_region *region = entry->physical.region;

          for (int i = 0; i < num_pages; i++) {
            uint32_t region_offset = get_total_page_size(i);

            space->pages[first_page + i] =
                pack_page_entry(region->handle, region_offset);
          }
        } break;

        case MAP_ENTRY_MMIO: {
          struct memory_region *region = entry->mmio.region;

          for (int i = 0; i < num_pages; i++) {
            uint32_t region_offset = get_total_page_size(i);

            space->pages[first_page + i] =
                pack_page_entry(region->handle, region_offset);
          }
        } break;

        case MAP_ENTRY_DEVICE: {
          struct address_map device_map = {0};
          entry->device.mapper(entry->device.device, space->dc, &device_map);

          as_merge_map(space, &device_map, addr);
        } break;

        case MAP_ENTRY_MIRROR: {
          CHECK(is_page_aligned(entry->mirror.physical_addr, size));

          int first_physical_page = get_page_index(entry->mirror.physical_addr);

          /* copy the page entries for the requested physical range into the new
             virtual address range */
          for (int i = 0; i < num_pages; i++) {
            space->pages[first_page + i] =
                space->pages[first_physical_page + i];
          }
        } break;
      }
    }
  }
}

static int as_num_adj_pages(struct address_space *space, int first_page_index) {
  int i;

  for (i = first_page_index; i < NUM_PAGES - 1; i++) {
    page_entry_t page = space->pages[i];
    page_entry_t next_page = space->pages[i + 1];

    int region_handle = get_region_handle(page);
    uint32_t region_offset = get_region_offset(page);
    const struct memory_region *region =
        &space->dc->memory->regions[region_handle];

    int next_region_handle = get_region_handle(next_page);
    uint32_t next_region_offset = get_region_offset(next_page);
    const struct memory_region *next_region =
        &space->dc->memory->regions[next_region_handle];

    if (next_region->type != region->type) {
      break;
    }

    if (region->type == REGION_PHYSICAL) {
      uint32_t page_delta =
          (next_region->physical.shmem_offset + next_region_offset) -
          (region->physical.shmem_offset + region_offset);

      if (page_delta != PAGE_SIZE) {
        break;
      }
    }
  }

  return (i + 1) - first_page_index;
}

void as_unmap(struct address_space *space) {
  for (int page_index = 0; page_index < NUM_PAGES;) {
    page_entry_t page = space->pages[page_index];

    if (!page) {
      page_index++;
      continue;
    }

    uint8_t *addr = space->base + get_total_page_size(page_index);
    int num_pages = as_num_adj_pages(space, page_index);
    uint32_t size = get_total_page_size(num_pages);

    CHECK(unmap_shared_memory(space->dc->memory->shmem, addr, size));

    page_index += num_pages;
  }
}

int as_map(struct address_space *space, const char *name,
           const struct address_map *map) {
  as_unmap(space);

  /* flatten the supplied address map out into a virtual page table */
  as_merge_map(space, map, 0);

#if 0
  LOG_INFO("===-----------------------------------------------------===");
  LOG_INFO("%s address space", name);
  LOG_INFO("===-----------------------------------------------------===");
#endif

  if (!reserve_address_space(&space->base)) {
    return 0;
  }

  /* iterate the virtual page table, mapping it to the reserved address space */
  for (int page_index = 0; page_index < NUM_PAGES;) {
    page_entry_t page = space->pages[page_index];

    if (!page) {
      page_index++;
      continue;
    }

    int region_handle = get_region_handle(page);
    uint32_t region_offset = get_region_offset(page);
    struct memory_region *region = &space->dc->memory->regions[region_handle];

    /* batch adjacent pages, mmap is fairly slow */
    uint8_t *addr = space->base + get_total_page_size(page_index);
    int num_pages = as_num_adj_pages(space, page_index);
    uint32_t size = get_total_page_size(num_pages);

#if 0
    LOG_INFO("[0x%08x, 0x%08x] %s+0x%x", addr, addr + size - 1, region->name,
             region_offset);
#endif

    if (region->type == REGION_PHYSICAL) {
      /* map virtual address range to backing shared memory object for physical
         regions */
      uint32_t shmem_offset = region->physical.shmem_offset + region_offset;

      if (!map_shared_memory(space->dc->memory->shmem, shmem_offset, addr, size,
                             ACC_READWRITE)) {
        return 0;
      }
    } else {
      /* disable access to virtual address range for mmio regions, resulting in
         SIGSEGV on access */
      if (!map_shared_memory(space->dc->memory->shmem, 0, addr, size,
                             ACC_NONE)) {
        return 0;
      }
    }

    page_index += num_pages;
  }

  return 1;
}

void as_destroy(struct address_space *space) {
  as_unmap(space);
  free(space);
}

struct address_space *as_create(struct dreamcast *dc) {
  struct address_space *space = calloc(1, sizeof(struct address_space));
  space->dc = dc;
  return space;
}
