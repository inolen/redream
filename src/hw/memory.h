#ifndef MEMORY_H
#define MEMORY_H

#include "core/assert.h"
#include "sys/memory.h"

struct dreamcast_s;

static const uint64_t ADDRESS_SPACE_SIZE = 1ull << 32;

typedef uint8_t (*r8_cb)(void *, uint32_t);
typedef uint16_t (*r16_cb)(void *, uint32_t);
typedef uint32_t (*r32_cb)(void *, uint32_t);
typedef uint64_t (*r64_cb)(void *, uint32_t);
typedef void (*w8_cb)(void *, uint32_t, uint8_t);
typedef void (*w16_cb)(void *, uint32_t, uint16_t);
typedef void (*w32_cb)(void *, uint32_t, uint32_t);
typedef void (*w64_cb)(void *, uint32_t, uint64_t);

typedef struct memory_region_s {
  int handle;
  uint32_t shmem_offset;
  uint32_t size;
  bool dynamic;

  r8_cb read8;
  r16_cb read16;
  r32_cb read32;
  r64_cb read64;
  w8_cb write8;
  w16_cb write16;
  w32_cb write32;
  w64_cb write64;

  void *data;
} memory_region_t;

struct memory_s;

memory_region_t *memory_create_region(struct memory_s *memory, uint32_t size);
memory_region_t *memory_create_dynamic_region(
    struct memory_s *memory, uint32_t size, r8_cb r8, r16_cb r16, r32_cb r32,
    r64_cb r64, w8_cb w8, w16_cb w16, w32_cb w32, w64_cb w64, void *data);

bool memory_init(struct memory_s *memory);

struct memory_s *memory_create(struct dreamcast_s *dc);
void memory_destroy(struct memory_s *memory);

// macros to help generate static AddressMap creators
#define AM_DECLARE(name) \
  void name(void *, struct dreamcast_s *, address_map_t *);

#define AM_BEGIN(type, name)                                               \
  void name(void *that, struct dreamcast_s *machine, address_map_t *map) { \
    type *self = (type *)that;                                             \
    uint32_t begin = 0;                                                    \
    uint32_t size = 0;                                                     \
    uint32_t mask = 0xffffffff;                                            \
    (void) self;                                                           \
    (void) machine;                                                        \
    (void) begin;                                                          \
    (void) size;                                                           \
    (void) mask;
#define AM_RANGE(begin_, end_) \
  begin = begin_;              \
  size = end_ - begin_ + 1;    \
  mask = 0xffffffff;
#define AM_MASK(mask_) mask = mask_;
#define AM_MOUNT()                                          \
  {                                                         \
    static struct memory_region_s *region = NULL;           \
    if (!region) {                                          \
      region = memory_create_region(machine->memory, size); \
    }                                                       \
    am_mount_region(map, region, size, begin, mask);        \
  }
#define AM_HANDLE(r8, r16, r32, r64, w8, w16, w32, w64)                       \
  {                                                                           \
    static struct memory_region_s *region = NULL;                             \
    if (!region) {                                                            \
      region = memory_create_dynamic_region(                                  \
          machine->memory, size, r8, r16, r32, r64, w8, w16, w32, w64, self); \
    }                                                                         \
    am_mount_region(map, region, size, begin, mask);                          \
  }
#define AM_DEVICE(name, cb)                               \
  {                                                       \
    static device_t *device = NULL;                       \
    if (!device) {                                        \
      device = dc_get_device(machine, name);              \
    }                                                     \
    CHECK_NOTNULL(device);                                \
    am_mount_device(map, device, &cb, size, begin, mask); \
  }
#define AM_MIRROR(addr) am_mirror(map, addr, size, begin);
#define AM_END() }
// clang-format on

#define MAX_MAP_ENTRIES 1024

struct address_map_s;

typedef void (*address_map_cb)(void *, struct dreamcast_s *,
                               struct address_map_s *);

typedef enum {
  MAP_ENTRY_MOUNT,
  MAP_ENTRY_DEVICE,
  MAP_ENTRY_MIRROR,
} address_map_entry_type_t;

typedef struct {
  address_map_entry_type_t type;

  uint32_t size;
  uint32_t addr;
  uint32_t addr_mask;

  union {
    struct {
      struct memory_region_s *region;
    } mount;

    struct {
      void *device;
      address_map_cb mapper;
    } device;

    struct {
      uint32_t physical_addr;
    } mirror;
  };
} address_map_entry_t;

typedef struct address_map_s {
  address_map_entry_t entries[MAX_MAP_ENTRIES];
  int num_entries;
} address_map_t;

void am_mount_region(address_map_t *am, struct memory_region_s *region,
                     uint32_t size, uint32_t addr, uint32_t addr_mask);
void am_mount_device(address_map_t *am, void *device, address_map_cb mapper,
                     uint32_t size, uint32_t addr, uint32_t addr_mask);
void am_mirror(address_map_t *am, uint32_t physical_addr, uint32_t size,
               uint32_t addr);

// helpers for extracting page information out of a virtual address
static const int PAGE_BITS = 20;
static const int PAGE_OFFSET_BITS = 32 - PAGE_BITS;
static const int PAGE_BLKSIZE = 1 << PAGE_OFFSET_BITS;
static const int NUM_PAGES = 1 << PAGE_BITS;
static const uint32_t PAGE_OFFSET_MASK = PAGE_BLKSIZE - 1;
static const uint32_t PAGE_INDEX_MASK = ~PAGE_OFFSET_MASK;

// helpers for accessing region information out of a page table entry
static const int MAX_REGIONS = 1 << (PAGE_OFFSET_BITS - 1);
static const uintptr_t REGION_INDEX_MASK = MAX_REGIONS - 1;
static const uintptr_t REGION_TYPE_MASK = MAX_REGIONS;
static const uintptr_t REGION_OFFSET_MASK =
    ~(REGION_TYPE_MASK | REGION_INDEX_MASK);

typedef uintptr_t page_entry_t;

typedef struct address_space_s {
  struct dreamcast_s *dc;
  page_entry_t pages[NUM_PAGES];
  uint8_t *base;
  uint8_t *protected_base;
} address_space_t;

void as_memcpy_to_guest(address_space_t *space, uint32_t virtual_dest,
                        const void *ptr, uint32_t size);
void as_memcpy_to_host(address_space_t *space, void *ptr, uint32_t virtual_src,
                       uint32_t size);
void as_memcpy(address_space_t *space, uint32_t virtual_dest,
               uint32_t virtual_src, uint32_t size);
void as_lookup(address_space_t *space, uint32_t virtual_addr, uint8_t **ptr,
               memory_region_t **region, uint32_t *offset);

uint8_t as_read8(address_space_t *space, uint32_t addr);
uint16_t as_read16(address_space_t *space, uint32_t addr);
uint32_t as_read32(address_space_t *space, uint32_t addr);
uint64_t as_read64(address_space_t *space, uint32_t addr);
void as_write8(address_space_t *space, uint32_t addr, uint8_t value);
void as_write16(address_space_t *space, uint32_t addr, uint16_t value);
void as_write32(address_space_t *space, uint32_t addr, uint32_t value);
void as_write64(address_space_t *space, uint32_t addr, uint64_t value);

bool as_map(address_space_t *space, const address_map_t *map);
void as_unmap(address_space_t *space);
uint8_t *as_translate(address_space_t *space, uint32_t addr);
uint8_t *as_translate_protected(address_space_t *space, uint32_t addr);

address_space_t *as_create(struct dreamcast_s *dc);
void as_destroy(address_space_t *space);

#endif
