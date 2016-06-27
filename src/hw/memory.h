#ifndef MEMORY_H
#define MEMORY_H

#include "core/assert.h"
#include "sys/memory.h"

struct dreamcast;

#define ADDRESS_SPACE_SIZE (UINT64_C(1) << 32)

typedef uint8_t (*r8_cb)(void *, uint32_t);
typedef uint16_t (*r16_cb)(void *, uint32_t);
typedef uint32_t (*r32_cb)(void *, uint32_t);
typedef uint64_t (*r64_cb)(void *, uint32_t);
typedef void (*w8_cb)(void *, uint32_t, uint8_t);
typedef void (*w16_cb)(void *, uint32_t, uint16_t);
typedef void (*w32_cb)(void *, uint32_t, uint32_t);
typedef void (*w64_cb)(void *, uint32_t, uint64_t);

struct memory;

struct memory_region {
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
};

struct memory_region *memory_create_region(struct memory *memory,
                                           uint32_t size);
struct memory_region *memory_create_dynamic_region(
    struct memory *memory, uint32_t size, r8_cb r8, r16_cb r16, r32_cb r32,
    r64_cb r64, w8_cb w8, w16_cb w16, w32_cb w32, w64_cb w64, void *data);

bool memory_init(struct memory *memory);

struct memory *memory_create(struct dreamcast *dc);
void memory_destroy(struct memory *memory);

// macros to help generate static AddressMap creators
#define AM_DECLARE(name) \
  void name(void *, struct dreamcast *, struct address_map *);

#define AM_BEGIN(type, name)                                                  \
  void name(void *that, struct dreamcast *machine, struct address_map *map) { \
    type *self = that;                                                        \
    uint32_t begin = 0;                                                       \
    uint32_t size = 0;                                                        \
    uint32_t mask = 0xffffffff;                                               \
    (void)self;                                                               \
    (void)machine;                                                            \
    (void)begin;                                                              \
    (void)size;                                                               \
    (void)mask;
#define AM_RANGE(begin_, end_) \
  begin = begin_;              \
  size = end_ - begin_ + 1;    \
  mask = 0xffffffff;
#define AM_MASK(mask_) mask = mask_;
#define AM_MOUNT()                                   \
  {                                                  \
    struct memory_region *region =                   \
        memory_create_region(machine->memory, size); \
    am_mount_region(map, region, size, begin, mask); \
  }
#define AM_HANDLE(r8, r16, r32, r64, w8, w16, w32, w64)                     \
  {                                                                         \
    struct memory_region *region = memory_create_dynamic_region(            \
        machine->memory, size, r8, r16, r32, r64, w8, w16, w32, w64, self); \
    am_mount_region(map, region, size, begin, mask);                        \
  }
#define AM_DEVICE(name, cb)                               \
  {                                                       \
    struct device *device = dc_get_device(machine, name); \
    CHECK_NOTNULL(device);                                \
    am_mount_device(map, device, &cb, size, begin, mask); \
  }
#define AM_MIRROR(addr) am_mirror(map, addr, size, begin);
#define AM_END() }
// clang-format on

#define MAX_MAP_ENTRIES 1024

struct address_map;

typedef void (*address_map_cb)(void *, struct dreamcast *,
                               struct address_map *);

enum map_entry_type {
  MAP_ENTRY_MOUNT,
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
    } mount;

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

void am_mount_region(struct address_map *am, struct memory_region *region,
                     uint32_t size, uint32_t addr, uint32_t addr_mask);
void am_mount_device(struct address_map *am, void *device,
                     address_map_cb mapper, uint32_t size, uint32_t addr,
                     uint32_t addr_mask);
void am_mirror(struct address_map *am, uint32_t physical_addr, uint32_t size,
               uint32_t addr);

// helpers for extracting page information out of a virtual address
#define PAGE_BITS 20
#define PAGE_OFFSET_BITS (32 - PAGE_BITS)
#define PAGE_SIZE (1 << PAGE_OFFSET_BITS)
#define PAGE_OFFSET_MASK (uint32_t)(PAGE_SIZE - 1)
#define PAGE_INDEX_MASK (uint32_t)(~PAGE_OFFSET_MASK)
#define NUM_PAGES (1 << PAGE_BITS)

// helpers for accessing region information out of a page table entry
#define REGION_INDEX_MASK (uintptr_t)(MAX_REGIONS - 1)
#define REGION_TYPE_MASK (uintptr_t)(MAX_REGIONS)
#define REGION_OFFSET_MASK (uintptr_t)(~(REGION_TYPE_MASK | REGION_INDEX_MASK))
#define MAX_REGIONS (1 << (PAGE_OFFSET_BITS - 1))

typedef uintptr_t page_entry_t;

struct address_space {
  struct dreamcast *dc;
  page_entry_t pages[NUM_PAGES];
  uint8_t *base;
  uint8_t *protected_base;
};

void as_memcpy_to_guest(struct address_space *space, uint32_t virtual_dest,
                        const void *ptr, uint32_t size);
void as_memcpy_to_host(struct address_space *space, void *ptr,
                       uint32_t virtual_src, uint32_t size);
void as_memcpy(struct address_space *space, uint32_t virtual_dest,
               uint32_t virtual_src, uint32_t size);
void as_lookup(struct address_space *space, uint32_t virtual_addr,
               uint8_t **ptr, struct memory_region **region, uint32_t *offset);

uint8_t as_read8(struct address_space *space, uint32_t addr);
uint16_t as_read16(struct address_space *space, uint32_t addr);
uint32_t as_read32(struct address_space *space, uint32_t addr);
uint64_t as_read64(struct address_space *space, uint32_t addr);
void as_write8(struct address_space *space, uint32_t addr, uint8_t value);
void as_write16(struct address_space *space, uint32_t addr, uint16_t value);
void as_write32(struct address_space *space, uint32_t addr, uint32_t value);
void as_write64(struct address_space *space, uint32_t addr, uint64_t value);

bool as_map(struct address_space *space, const struct address_map *map);
void as_unmap(struct address_space *space);
uint8_t *as_translate(struct address_space *space, uint32_t addr);
uint8_t *as_translate_protected(struct address_space *space, uint32_t addr);

struct address_space *as_create(struct dreamcast *dc);
void as_destroy(struct address_space *space);

#endif
