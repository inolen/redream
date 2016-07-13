#include "hw/memory.h"
#include "core/math.h"
#include "core/string.h"
#include "hw/dreamcast.h"
#include "sys/exception_handler.h"

struct memory {
  struct dreamcast *dc;

  shmem_handle_t shmem;
  uint32_t shmem_size;

  struct physical_region physical_regions[MAX_REGIONS];
  int num_physical_regions;
  struct mmio_region mmio_regions[MAX_REGIONS];
  int num_mmio_regions;
};

static int is_page_aligned(uint32_t start, uint32_t size) {
  return (start & ((1 << PAGE_OFFSET_BITS) - 1)) == 0 &&
         ((start + size) & ((1 << PAGE_OFFSET_BITS) - 1)) == 0;
}

static int get_total_page_size(int num_pages) {
  return (uint32_t)num_pages * PAGE_SIZE;
}

// map virtual addresses to pages
static int get_page_index(uint32_t addr) {
  return addr >> PAGE_OFFSET_BITS;
}

static uint32_t get_page_offset(uint32_t addr) {
  return addr & PAGE_OFFSET_MASK;
}

// pack and unpack page entry bitstrings
static page_entry_t pack_page_entry(int physical_handle,
                                    uint32_t physical_offset, int mmio_handle,
                                    uint32_t mmio_offset) {
  return ((page_entry_t)(physical_offset | physical_handle) << 32) |
         (mmio_handle | mmio_offset);
}

static uint32_t get_physical_offset(page_entry_t page) {
  return (page >> 32) & REGION_OFFSET_MASK;
}

static int get_physical_handle(page_entry_t page) {
  return (page >> 32) & REGION_HANDLE_MASK;
}

static int get_mmio_offset(page_entry_t page) {
  return page & REGION_OFFSET_MASK;
}

static int get_mmio_handle(page_entry_t page) {
  return page & REGION_HANDLE_MASK;
}

// iterate mirrors for a given address and mask
struct mirror_iterator {
  uint32_t base, mask, imask, step;
  uint32_t i, addr;
  bool first;
};

static void mirror_iterator_init(struct mirror_iterator *it, uint32_t addr,
                                 uint32_t mask) {
  it->base = addr & mask;
  it->mask = mask;
  it->imask = ~mask;
  it->step = 1 << ctz32(it->imask);
  it->i = 0;
  it->addr = it->base;
  it->first = true;
}

static bool mirror_iterator_next(struct mirror_iterator *it) {
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

struct physical_region *memory_create_physical_region(struct memory *memory,
                                                      uint32_t size) {
  CHECK_LT(memory->num_physical_regions, MAX_REGIONS);

  memory->num_physical_regions++;

  struct physical_region *region =
      &memory->physical_regions[memory->num_physical_regions];
  region->handle = memory->num_physical_regions;
  region->shmem_offset = memory->shmem_size;
  region->size = size;

  // ensure the shared memory regions are aligned to the allocation granularity,
  // otherwise it will confusingly fail to map further down the line
  size_t granularity = get_allocation_granularity();
  CHECK((memory->shmem_size & (granularity - 1)) == 0 &&
        ((memory->shmem_size + size) & (granularity - 1)) == 0);
  memory->shmem_size += size;

  return region;
}

struct mmio_region *memory_create_mmio_region(struct memory *memory,
                                              uint32_t size, void *data,
                                              r8_cb r8, r16_cb r16, r32_cb r32,
                                              r64_cb r64, w8_cb w8, w16_cb w16,
                                              w32_cb w32, w64_cb w64) {
  CHECK_LT(memory->num_mmio_regions, MAX_REGIONS);

  memory->num_mmio_regions++;

  struct mmio_region *region = &memory->mmio_regions[memory->num_mmio_regions];
  region->handle = memory->num_mmio_regions;
  region->size = size;
  region->data = data;
  region->read8 = r8;
  region->read16 = r16;
  region->read32 = r32;
  region->read64 = r64;
  region->write8 = w8;
  region->write16 = w16;
  region->write32 = w32;
  region->write64 = w64;

  return region;
}

static bool memory_create_shmem(struct memory *memory) {
  // create the shared memory object to back the address space
  memory->shmem =
      create_shared_memory("/redream", ADDRESS_SPACE_SIZE, ACC_READWRITE);

  if (memory->shmem == SHMEM_INVALID) {
    LOG_WARNING("Failed to create shared memory object");
    return false;
  }

  return true;
}

static void memory_destroy_shmem(struct memory *memory) {
  destroy_shared_memory(memory->shmem);
}

bool memory_init(struct memory *memory) {
  if (!memory_create_shmem(memory)) {
    return false;
  }

  // map each memory interface's address space
  list_for_each_entry(dev, &memory->dc->devices, struct device, it) {
    if (dev->memory) {
      // create the actual address map
      struct address_map map = {};
      dev->memory->mapper(dev, memory->dc, &map);

      // apply the map to create the address space
      CHECK(as_map(dev->memory->space, &map));
    }
  }

  return true;
}

struct memory *memory_create(struct dreamcast *dc) {
  struct memory *memory = calloc(1, sizeof(struct memory));

  memory->dc = dc;
  memory->shmem = SHMEM_INVALID;
  // 0 page is reserved, meaning all valid page entries must be non-zero
  memory->num_physical_regions = 1;

  return memory;
}

void memory_destroy(struct memory *memory) {
  memory_destroy_shmem(memory);
  free(memory);
}

static struct address_map_entry *address_map_alloc_entry(
    struct address_map *am) {
  CHECK_LT(am->num_entries, MAX_MAP_ENTRIES);
  struct address_map_entry *entry = &am->entries[am->num_entries++];
  memset(entry, 0, sizeof(*entry));
  return entry;
}

void am_physical(struct address_map *am, struct physical_region *region,
                 uint32_t size, uint32_t addr, uint32_t addr_mask) {
  struct address_map_entry *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_PHYSICAL;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = addr_mask;
  entry->physical.region = region;
}

void am_mmio(struct address_map *am, struct mmio_region *region, uint32_t size,
             uint32_t addr, uint32_t addr_mask) {
  struct address_map_entry *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_MMIO;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = addr_mask;
  entry->mmio.region = region;
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

void am_mirror(struct address_map *am, uint32_t physical_addr, uint32_t size,
               uint32_t addr) {
  struct address_map_entry *entry = address_map_alloc_entry(am);
  entry->type = MAP_ENTRY_MIRROR;
  entry->size = size;
  entry->addr = addr;
  entry->addr_mask = 0xffffffff;
  entry->mirror.physical_addr = physical_addr;
}

#define define_read_bytes(name, type)                               \
  type as_##name(struct address_space *space, uint32_t addr) {      \
    page_entry_t page = space->pages[get_page_index(addr)];         \
    DCHECK(page);                                                   \
    int mmio_handle = get_mmio_handle(page);                        \
    if (!mmio_handle) {                                             \
      return *(type *)(space->base + addr);                         \
    }                                                               \
    struct mmio_region *region =                                    \
        &space->dc->memory->mmio_regions[mmio_handle];              \
    uint32_t region_offset = get_mmio_offset(page);                 \
    uint32_t page_offset = get_page_offset(addr);                   \
    return region->name(region->data, region_offset + page_offset); \
  }

define_read_bytes(read8, uint8_t);
define_read_bytes(read16, uint16_t);
define_read_bytes(read32, uint32_t);
define_read_bytes(read64, uint64_t);

#define define_write_bytes(name, type)                                     \
  void as_##name(struct address_space *space, uint32_t addr, type value) { \
    page_entry_t page = space->pages[get_page_index(addr)];                \
    DCHECK(page);                                                          \
    int mmio_handle = get_mmio_handle(page);                               \
    if (!mmio_handle) {                                                    \
      *(type *)(space->base + addr) = value;                               \
      return;                                                              \
    }                                                                      \
    struct mmio_region *region =                                           \
        &space->dc->memory->mmio_regions[mmio_handle];                     \
    uint32_t region_offset = get_mmio_offset(page);                        \
    uint32_t page_offset = get_page_offset(addr);                          \
    region->name(region->data, region_offset + page_offset, value);        \
  }

define_write_bytes(write8, uint8_t);
define_write_bytes(write16, uint16_t);
define_write_bytes(write32, uint32_t);
define_write_bytes(write64, uint64_t);

void as_memcpy_to_guest(struct address_space *space, uint32_t dst,
                        const void *ptr, uint32_t size) {
  CHECK(size % 4 == 0);

  const uint8_t *src = ptr;
  uint32_t end = dst + size;
  while (dst < end) {
    as_write32(space, dst, *(uint32_t *)src);
    dst += 4;
    src += 4;
  }
}

void as_memcpy_to_host(struct address_space *space, void *ptr, uint32_t src,
                       uint32_t size) {
  CHECK(size % 4 == 0);

  uint8_t *dst = ptr;
  uint8_t *end = dst + size;
  while (dst < end) {
    *(uint32_t *)dst = as_read32(space, src);
    src += 4;
    dst += 4;
  }
}

void as_memcpy(struct address_space *space, uint32_t dst, uint32_t src,
               uint32_t size) {
  CHECK(size % 4 == 0);

  uint32_t end = dst + size;
  while (dst < end) {
    as_write32(space, dst, as_read32(space, src));
    src += 4;
    dst += 4;
  }
}

void as_lookup(struct address_space *space, uint32_t addr, uint8_t **ptr,
               struct physical_region **physical_region,
               uint32_t *physical_offset, struct mmio_region **mmio_region,
               uint32_t *mmio_offset) {
  page_entry_t page = space->pages[get_page_index(addr)];
  int physical_handle = get_physical_handle(page);
  int mmio_handle = get_mmio_handle(page);

  *ptr = space->base + addr;
  *physical_region = physical_handle
                         ? &space->dc->memory->physical_regions[physical_handle]
                         : NULL;
  *physical_offset = get_physical_offset(page) + get_page_offset(addr);
  *mmio_region =
      mmio_handle ? &space->dc->memory->mmio_regions[mmio_handle] : NULL;
  *mmio_offset = get_mmio_offset(page) + get_page_offset(addr);
}

static void as_merge_map(struct address_space *space,
                         const struct address_map *map, uint32_t offset) {
  // iterate regions in the supplied memory map in the other added, flattening
  // them out into a virtual page table
  for (int i = 0, n = map->num_entries; i < n; i++) {
    const struct address_map_entry *entry = &map->entries[i];

    // iterate each mirror of the entry
    struct mirror_iterator it = {};

    mirror_iterator_init(&it, offset + entry->addr, entry->addr_mask);

    while (mirror_iterator_next(&it)) {
      uint32_t addr = it.addr;
      uint32_t size = entry->size;
      CHECK(is_page_aligned(addr, size));

      int first_page = get_page_index(addr);
      int num_pages = size >> PAGE_OFFSET_BITS;

      switch (entry->type) {
        case MAP_ENTRY_PHYSICAL: {
          struct physical_region *physical_region = entry->physical.region;

          for (int i = 0; i < num_pages; i++) {
            uint32_t physical_offset = get_total_page_size(i);

            space->pages[first_page + i] =
                pack_page_entry(physical_region->handle, physical_offset, 0, 0);
          }
        } break;

        case MAP_ENTRY_MMIO: {
          struct mmio_region *mmio_region = entry->mmio.region;

          for (int i = 0; i < num_pages; i++) {
            uint32_t mmio_offset = get_total_page_size(i);

            page_entry_t page = space->pages[first_page + i];
            int physical_handle = get_physical_handle(page);
            uint32_t physical_offset = get_physical_offset(page);

            space->pages[first_page + i] =
                pack_page_entry(physical_handle, physical_offset,
                                mmio_region->handle, mmio_offset);
          }
        } break;

        case MAP_ENTRY_DEVICE: {
          struct address_map device_map = {};
          entry->device.mapper(entry->device.device, space->dc, &device_map);
          as_merge_map(space, &device_map, addr);
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

static uint32_t as_get_page_offset(struct address_space *space,
                                   page_entry_t page) {
  const struct physical_region *region =
      &space->dc->memory->physical_regions[get_physical_handle(page)];
  return region->shmem_offset + get_physical_offset(page);
}

static int as_num_adj_pages(struct address_space *space, int first_page_index) {
  int i;

  for (i = first_page_index; i < NUM_PAGES - 1; i++) {
    page_entry_t page = space->pages[i];
    page_entry_t next_page = space->pages[i + 1];

    uint32_t page_offset = as_get_page_offset(space, page);
    uint32_t next_page_offset = as_get_page_offset(space, next_page);

    if ((next_page_offset - page_offset) != PAGE_SIZE) {
      break;
    }
  }

  return (i + 1) - first_page_index;
}

static bool as_map_pages(struct address_space *space, uint8_t *base) {
  for (int page_index = 0; page_index < NUM_PAGES;) {
    page_entry_t page = space->pages[page_index];

    if (!page) {
      page_index++;
      continue;
    }

    // batch map djacent pages, mmap is fairly slow
    int num_pages = as_num_adj_pages(space, page_index);
    uint32_t size = get_total_page_size(num_pages);

    // mmap the virtual address range to the raw address space
    uint32_t addr = get_total_page_size(page_index);
    uint32_t page_offset = as_get_page_offset(space, page);

    if (!map_shared_memory(space->dc->memory->shmem, page_offset, base + addr,
                           size, ACC_READWRITE)) {
      return false;
    }

    page_index += num_pages;
  }

  return true;
}

bool as_map(struct address_space *space, const struct address_map *map) {
  as_unmap(space);

  // flatten the supplied address map out into a virtual page table
  as_merge_map(space, map, 0);

  // map the virtual page table into both the base and protected mirrors
  if (!reserve_address_space(&space->base) ||
      !as_map_pages(space, space->base)) {
    return false;
  }

  if (!reserve_address_space(&space->protected_base) ||
      !as_map_pages(space, space->protected_base)) {
    return false;
  }

  // protect dynamic regions in the protected address space
  for (int page_index = 0; page_index < NUM_PAGES; page_index++) {
    page_entry_t page = space->pages[page_index];
    int mmio_index = get_mmio_handle(page);

    if (!mmio_index) {
      continue;
    }

    uint32_t addr = get_total_page_size(page_index);
    protect_pages(space->protected_base + addr, PAGE_SIZE, ACC_NONE);
  }

  return true;
}

static void as_unmap_pages(struct address_space *space, uint8_t *base) {
  if (!base) {
    return;
  }

  for (int page_index = 0; page_index < NUM_PAGES;) {
    page_entry_t page = space->pages[page_index];

    if (!page) {
      page_index++;
      continue;
    }

    uint32_t addr = get_total_page_size(page_index);

    int num_pages = as_num_adj_pages(space, page_index);
    uint32_t size = get_total_page_size(num_pages);

    CHECK(unmap_shared_memory(space->dc->memory->shmem, base + addr, size));

    page_index += num_pages;
  }
}

void as_unmap(struct address_space *space) {
  as_unmap_pages(space, space->base);
  as_unmap_pages(space, space->protected_base);
}

uint8_t *as_translate(struct address_space *space, uint32_t addr) {
  return space->base + addr;
}

uint8_t *as_translate_protected(struct address_space *space, uint32_t addr) {
  return space->protected_base + addr;
}

struct address_space *as_create(struct dreamcast *dc) {
  struct address_space *space = calloc(1, sizeof(struct address_space));
  space->dc = dc;
  return space;
}

void as_destroy(struct address_space *space) {
  as_unmap(space);
  free(space);
}
