#include <string.h>
#include "core/math.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "sys/exception_handler.h"

typedef struct memory_s {
  dreamcast_t *dc;

  shmem_handle_t shmem;
  uint32_t shmem_size;

  memory_region_t regions[MAX_REGIONS];
  int num_regions;
} memory_t;

static bool memory_create_shmem(memory_t *memory);
static void memory_destroy_shmem(memory_t *memory);
static memory_region_t *memory_alloc_region(memory_t *memory, uint32_t size);

static void address_space_merge_map(address_space_t *space,
                                    const address_map_t *map, uint32_t offset);
static uint32_t address_space_get_page_offset(address_space_t *space,
                                              page_entry_t page);
static int address_space_num_adj_pages(address_space_t *space,
                                       int first_page_index);
static bool address_space_map_pages(address_space_t *space, uint8_t *base);
static void address_space_unmap_pages(address_space_t *space, uint8_t *base);

static bool is_page_aligned(uint32_t start, uint32_t size) {
  return (start & (PAGE_OFFSET_BITS - 1)) == 0 &&
         ((start + size) & (PAGE_OFFSET_BITS - 1)) == 0;
}

// map virtual addresses to pages
static int get_page_index(uint32_t addr) {
  return addr >> PAGE_OFFSET_BITS;
}

static uint32_t get_page_offset(uint32_t addr) {
  return addr & PAGE_OFFSET_MASK;
}

// pack and unpack page entry bitstrings
static page_entry_t pack_page_entry(const memory_region_t *region,
                                    uint32_t region_offset) {
  return region_offset | (region->dynamic ? 0 : REGION_TYPE_MASK) |
         region->handle;
}

static uint32_t get_region_offset(page_entry_t page) {
  return page & REGION_OFFSET_MASK;
}

static int is_region_static(page_entry_t page) {
  return page & REGION_TYPE_MASK;
}

static int get_region_index(page_entry_t page) {
  return page & REGION_INDEX_MASK;
}

// iterate mirrors for a given address and mask
typedef struct {
  uint32_t base, mask, imask, step;
  uint32_t i, addr;
  bool first;
} mirror_iterator_t;

static void mirror_iterator_init(mirror_iterator_t *it, uint32_t addr,
                                 uint32_t mask) {
  it->base = addr & mask;
  it->mask = mask;
  it->imask = ~mask;
  it->step = 1 << ctz32(it->imask);
  it->i = 0;
  it->addr = it->base;
  it->first = true;
}

static bool mirror_iterator_next(mirror_iterator_t *it) {
  // first iteration just returns base
  if (it->first) {
    it->first = false;
    return true;
  }

  // stop once mask complement is completely set
  if ((it->addr & it->imask) == it->imask) {
    return false;
  }

  // step to the next permutation
  it->i += it->step;

  // if the new value carries over into a masked off bit, skip it
  uint32_t carry;
  do {
    carry = it->i & it->mask;
    it->i += carry;
  } while (carry);

  // merge with the base
  it->addr = it->base | it->i;

  return true;
}

static bool reserve_address_space(uint8_t **base) {
  // find a contiguous (1 << 32) byte chunk of memory to map an address space to
  int i = 64;

  while (i > 32) {
    i--;

    *base = (uint8_t *)(1ull << i);

    if (!reserve_pages(*base, ADDRESS_SPACE_SIZE)) {
      continue;
    }

    // reservation was a success, release now so shared memory can be mapped
    // into it
    release_pages(*base, ADDRESS_SPACE_SIZE);

    return true;
  }

  LOG_WARNING("Failed to reserve address space");

  return false;
}

memory_t *memory_create(dreamcast_t *dc) {
  memory_t *memory = calloc(1, sizeof(memory_t));

  memory->dc = dc;
  memory->shmem = SHMEM_INVALID;
  // 0 page is reserved, meaning all valid page entries must be non-zero
  memory->num_regions = 1;

  return memory;
}

void memory_destroy(memory_t *memory) {
  memory_destroy_shmem(memory);
  free(memory);
}

bool memory_init(memory_t *memory) {
  if (!memory_create_shmem(memory)) {
    return false;
  }

  // map each memory interface's address space
  device_t *dev = memory->dc->devices;

  while (dev) {
    if (dev->memory) {
      // create the actual address map
      address_map_t map = {};
      dev->memory->mapper(dev, memory->dc, &map);

      // apply the map to create the address space
      CHECK(address_space_map(dev->memory->space, &map));
    }

    dev = dev->next;
  }

  return true;
}

memory_region_t *memory_create_region(memory_t *memory, uint32_t size) {
  CHECK(is_page_aligned(memory->shmem_size, size));

  memory_region_t *region = memory_alloc_region(memory, size);
  region->dynamic = false;

  return region;
}

memory_region_t *memory_create_dynamic_region(memory_t *memory, uint32_t size,
                                              r8_cb r8, r16_cb r16, r32_cb r32,
                                              r64_cb r64, w8_cb w8, w16_cb w16,
                                              w32_cb w32, w64_cb w64,
                                              void *data) {
  memory_region_t *region = memory_alloc_region(memory, size);

  region->dynamic = true;
  region->r8 = r8;
  region->r16 = r16;
  region->r32 = r32;
  region->r64 = r64;
  region->w8 = w8;
  region->w16 = w16;
  region->w32 = w32;
  region->w64 = w64;
  region->data = data;

  return region;
}

bool memory_create_shmem(memory_t *memory) {
  // create the shared memory object to back the address space
  memory->shmem =
      create_shared_memory("/redream", ADDRESS_SPACE_SIZE, ACC_READWRITE);

  if (memory->shmem == SHMEM_INVALID) {
    LOG_WARNING("Failed to create shared memory object");
    return false;
  }

  return true;
}

void memory_destroy_shmem(memory_t *memory) {
  destroy_shared_memory(memory->shmem);
}

memory_region_t *memory_alloc_region(memory_t *memory, uint32_t size) {
  CHECK_LT(memory->num_regions, MAX_REGIONS);
  CHECK(is_page_aligned(memory->shmem_size, size));

  memory_region_t *region = &memory->regions[memory->num_regions];
  memset(region, 0, sizeof(*region));
  region->handle = memory->num_regions++;
  region->shmem_offset = memory->shmem_size;
  region->size = size;

  memory->shmem_size += size;

  return region;
}

static address_map_entry_t *address_map_alloc_entry(address_map_t *am) {
  CHECK_LT(am->num_entries, MAX_MAP_ENTRIES);
  address_map_entry_t *entry = &am->entries[am->num_entries++];
  memset(entry, 0, sizeof(*entry));
  return entry;
}

void address_map_mount_region(address_map_t *am, struct memory_region_s *region,
                              uint32_t size, uint32_t addr,
                              uint32_t addr_mask) {
  address_map_entry_t *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_MOUNT;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = addr_mask;
  entry->mount.region = region;
}

void address_map_mount_device(address_map_t *am, void *device,
                              address_map_cb mapper, uint32_t size,
                              uint32_t addr, uint32_t addr_mask) {
  address_map_entry_t *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_DEVICE;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = addr_mask;
  entry->device.device = device;
  entry->device.mapper = mapper;
}

void address_map_mirror(address_map_t *am, uint32_t physical_addr,
                        uint32_t size, uint32_t addr) {
  address_map_entry_t *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_MIRROR;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = 0xffffffff;
  entry->mirror.physical_addr = physical_addr;
}

address_space_t *address_space_create(dreamcast_t *dc) {
  address_space_t *space = calloc(1, sizeof(address_space_t));
  space->dc = dc;
  return space;
}

void address_space_destroy(address_space_t *space) {
  address_space_unmap(space);
  free(space);
}

bool address_space_map(address_space_t *space, const address_map_t *map) {
  address_space_unmap(space);

  // flatten the supplied address map out into a virtual page table
  address_space_merge_map(space, map, 0);

  // map the virtual page table into both the base and protected mirrors
  if (!reserve_address_space(&space->base) ||
      !address_space_map_pages(space, space->base)) {
    return false;
  }

  if (!reserve_address_space(&space->protected_base) ||
      !address_space_map_pages(space, space->protected_base)) {
    return false;
  }

  // protect dynamic regions in the protected address space
  for (int page_index = 0; page_index < NUM_PAGES; page_index++) {
    page_entry_t page = space->pages[page_index];

    if (is_region_static(page)) {
      continue;
    }

    uint32_t addr = page_index * PAGE_BLKSIZE;
    protect_pages(space->protected_base + addr, PAGE_BLKSIZE, ACC_NONE);
  }

  return true;
}

void address_space_unmap(address_space_t *space) {
  address_space_unmap_pages(space, space->base);
  address_space_unmap_pages(space, space->protected_base);
}

uint8_t *address_space_translate(address_space_t *space, uint32_t addr) {
  return space->base + addr;
}

uint8_t *address_space_translate_protected(address_space_t *space,
                                           uint32_t addr) {
  return space->protected_base + addr;
}

#define define_read_bytes(name, type)                                \
  type address_space_##name(address_space_t *space, uint32_t addr) { \
    page_entry_t page = space->pages[get_page_index(addr)];          \
    DCHECK(page);                                                    \
                                                                     \
    if (is_region_static(page)) {                                    \
      return *(type *)(space->base + addr);                          \
    }                                                                \
                                                                     \
    memory_region_t *region =                                        \
        &space->dc->memory->regions[get_region_index(page)];         \
    uint32_t region_offset = get_region_offset(page);                \
    uint32_t page_offset = get_page_offset(addr);                    \
                                                                     \
    return region->name(region->data, region_offset + page_offset);  \
  }

define_read_bytes(r8, uint8_t);
define_read_bytes(r16, uint16_t);
define_read_bytes(r32, uint32_t);
define_read_bytes(r64, uint64_t);

#define define_write_bytes(name, type)                              \
  void address_space_##name(address_space_t *space, uint32_t addr,  \
                            type value) {                           \
    page_entry_t page = space->pages[get_page_index(addr)];         \
    DCHECK(page);                                                   \
                                                                    \
    if (is_region_static(page)) {                                   \
      *(type *)(space->base + addr) = value;                        \
      return;                                                       \
    }                                                               \
                                                                    \
    memory_region_t *region =                                       \
        &space->dc->memory->regions[get_region_index(page)];        \
    uint32_t region_offset = get_region_offset(page);               \
    uint32_t page_offset = get_page_offset(addr);                   \
                                                                    \
    region->name(region->data, region_offset + page_offset, value); \
  }

define_write_bytes(w8, uint8_t);
define_write_bytes(w16, uint16_t);
define_write_bytes(w32, uint32_t);
define_write_bytes(w64, uint64_t);

void address_space_memcpy_to_guest(address_space_t *space, uint32_t dst,
                                   const void *ptr, uint32_t size) {
  CHECK(size % 4 == 0);

  const uint8_t *src = ptr;
  uint32_t end = dst + size;
  while (dst < end) {
    address_space_w32(space, dst, *(uint32_t *)src);
    dst += 4;
    src += 4;
  }
}

void address_space_memcpy_to_host(address_space_t *space, void *ptr,
                                  uint32_t src, uint32_t size) {
  CHECK(size % 4 == 0);

  uint8_t *dst = ptr;
  uint8_t *end = dst + size;
  while (dst < end) {
    *(uint32_t *)dst = address_space_r32(space, src);
    src += 4;
    dst += 4;
  }
}

void address_space_memcpy(address_space_t *space, uint32_t dst, uint32_t src,
                          uint32_t size) {
  CHECK(size % 4 == 0);

  uint32_t end = dst + size;
  while (dst < end) {
    address_space_w32(space, dst, address_space_r32(space, src));
    src += 4;
    dst += 4;
  }
}

void address_space_lookup(address_space_t *space, uint32_t addr, uint8_t **ptr,
                          memory_region_t **region, uint32_t *offset) {
  page_entry_t page = space->pages[get_page_index(addr)];

  if (is_region_static(page)) {
    *ptr = space->base + addr;
  } else {
    *ptr = NULL;
  }

  *region = &space->dc->memory->regions[get_region_index(page)];
  *offset = get_region_offset(page) + get_page_offset(addr);
}

void address_space_merge_map(address_space_t *space, const address_map_t *map,
                             uint32_t offset) {
  // iterate regions in the supplied memory map in the other added, flattening
  // them out into a virtual page table
  for (int i = 0, n = map->num_entries; i < n; i++) {
    const address_map_entry_t *entry = &map->entries[i];

    // iterate each mirror of the entry
    mirror_iterator_t it = {};

    mirror_iterator_init(&it, offset + entry->addr, entry->addr_mask);

    while (mirror_iterator_next(&it)) {
      uint32_t addr = it.addr;
      uint32_t size = entry->size;
      CHECK(is_page_aligned(addr, size));

      int first_page = get_page_index(addr);
      int num_pages = size >> PAGE_OFFSET_BITS;

      switch (entry->type) {
        case MAP_ENTRY_MOUNT: {
          memory_region_t *region = entry->mount.region;

          // create an entry in the page table for each page the region occupies
          for (int i = 0; i < num_pages; i++) {
            uint32_t region_offset = i * PAGE_BLKSIZE;

            space->pages[first_page + i] =
                pack_page_entry(region, region_offset);
          }
        } break;

        case MAP_ENTRY_DEVICE: {
          address_map_t device_map = {};
          entry->device.mapper(entry->device.device, space->dc, &device_map);
          address_space_merge_map(space, &device_map, addr);
        } break;

        case MAP_ENTRY_MIRROR: {
          CHECK(is_page_aligned(entry->mirror.physical_addr, size));

          int first_physical_page = get_page_index(entry->mirror.physical_addr);

          // copy the page entries for the requested physical range into the new
          // virtual address range
          for (int i = 0; i < num_pages; i++) {
            space->pages[first_page + i] =
                space->pages[first_physical_page + i];
          }
        } break;
      }
    }
  }
}

uint32_t address_space_get_page_offset(address_space_t *space,
                                       page_entry_t page) {
  const memory_region_t *region =
      &space->dc->memory->regions[get_region_index(page)];
  return region->shmem_offset + get_region_offset(page);
}

int address_space_num_adj_pages(address_space_t *space, int first_page_index) {
  int i;

  for (i = first_page_index; i < NUM_PAGES - 1; i++) {
    page_entry_t page = space->pages[i];
    page_entry_t next_page = space->pages[i + 1];

    uint32_t page_offset = address_space_get_page_offset(space, page);
    uint32_t next_page_offset = address_space_get_page_offset(space, next_page);

    if ((next_page_offset - page_offset) != PAGE_BLKSIZE) {
      break;
    }
  }

  return (i + 1) - first_page_index;
}

bool address_space_map_pages(address_space_t *space, uint8_t *base) {
  for (int page_index = 0; page_index < NUM_PAGES;) {
    page_entry_t page = space->pages[page_index];

    if (!page) {
      page_index++;
      continue;
    }

    // batch map djacent pages, mmap is fairly slow
    int num_pages = address_space_num_adj_pages(space, page_index);
    uint32_t size = num_pages * PAGE_BLKSIZE;

    // mmap the virtual address range to the raw address space
    uint32_t addr = page_index * PAGE_BLKSIZE;
    uint32_t page_offset = address_space_get_page_offset(space, page);

    if (!map_shared_memory(space->dc->memory->shmem, page_offset, base + addr,
                           size, ACC_READWRITE)) {
      return false;
    }

    page_index += num_pages;
  }

  return true;
}

void address_space_unmap_pages(address_space_t *space, uint8_t *base) {
  if (!base) {
    return;
  }

  for (int page_index = 0; page_index < NUM_PAGES;) {
    page_entry_t page = space->pages[page_index];

    if (!page) {
      page_index++;
      continue;
    }

    uint32_t addr = page_index * PAGE_BLKSIZE;

    int num_pages = address_space_num_adj_pages(space, page_index);
    uint32_t size = num_pages * PAGE_BLKSIZE;

    CHECK(unmap_shared_memory(space->dc->memory->shmem, base + addr, size));

    page_index += num_pages;
  }
}
