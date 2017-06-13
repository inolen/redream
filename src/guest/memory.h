#ifndef MEMORY_H
#define MEMORY_H

#include "core/assert.h"
#include "core/math.h"
#include "core/memory.h"

struct address_map;
struct address_space;
struct dreamcast;
struct memory;

/* helpers for mmio callbacks, assume data is always a uint32_t */
#define DATA_SIZE() (ctz64((uint64_t)data_mask + 1) >> 3)
#define READ_DATA(ptr) ((*(uint32_t *)(ptr)) & data_mask)
#define WRITE_DATA(ptr) \
  (*(uint32_t *)(ptr) = (*(uint32_t *)(ptr) & ~data_mask) | (data & data_mask))

/* helpers for generating address map functions */
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
#define AM_MOUNT(name)                                              \
  {                                                                 \
    struct memory_region *region =                                  \
        memory_create_physical_region(machine->memory, name, size); \
    am_physical(map, region, size, begin, mask);                    \
  }
#define AM_HANDLE(name, read, write, read_string, write_string)            \
  {                                                                        \
    struct memory_region *region =                                         \
        memory_create_mmio_region(machine->memory, name, size, self, read, \
                                  write, read_string, write_string);       \
    am_mmio(map, region, size, begin, mask);                               \
  }

#define AM_DEVICE(name, cb)                               \
  {                                                       \
    struct device *device = dc_get_device(machine, name); \
    CHECK_NOTNULL(device);                                \
    am_device(map, device, &cb, size, begin, mask);       \
  }
#define AM_MIRROR(addr) am_mirror(map, addr, size, begin);
#define AM_END() }

/* memory */
typedef uint32_t (*mmio_read_cb)(void *, uint32_t, uint32_t);
typedef void (*mmio_write_cb)(void *, uint32_t, uint32_t, uint32_t);
typedef void (*mmio_read_string_cb)(void *, void *, uint32_t, int);
typedef void (*mmio_write_string_cb)(void *, uint32_t, const void *, int);

struct memory *memory_create(struct dreamcast *dc);
void memory_destroy(struct memory *memory);
int memory_init(struct memory *memory);

uint8_t *memory_translate(struct memory *memory, const char *name,
                          uint32_t offset);

struct memory_region *memory_create_physical_region(struct memory *memory,
                                                    const char *name,
                                                    uint32_t size);
struct memory_region *memory_create_mmio_region(
    struct memory *memory, const char *name, uint32_t size, void *data,
    mmio_read_cb read, mmio_write_cb write, mmio_read_string_cb read_string,
    mmio_write_string_cb write_string);

/* address map */
typedef void (*address_map_cb)(void *, struct dreamcast *,
                               struct address_map *);

void am_physical(struct address_map *am, struct memory_region *region,
                 uint32_t size, uint32_t addr, uint32_t addr_mask);
void am_mmio(struct address_map *am, struct memory_region *region,
             uint32_t size, uint32_t addr, uint32_t addr_mask);
void am_device(struct address_map *am, void *device, address_map_cb mapper,
               uint32_t size, uint32_t addr, uint32_t addr_mask);
void am_mirror(struct address_map *am, uint32_t physical_addr, uint32_t size,
               uint32_t addr);

/* address space */
struct address_space *as_create(struct dreamcast *dc);
void as_destroy(struct address_space *space);

int as_map(struct address_space *space, const char *name,
           const struct address_map *map);
void as_unmap(struct address_space *space);

void as_lookup(struct address_space *space, uint32_t addr, void **ptr,
               void **userdata, mmio_read_cb *read, mmio_write_cb *write,
               uint32_t *offset);
uint8_t *as_translate(struct address_space *space, uint32_t addr);

uint8_t as_read8(struct address_space *space, uint32_t addr);
uint16_t as_read16(struct address_space *space, uint32_t addr);
uint32_t as_read32(struct address_space *space, uint32_t addr);
void as_write8(struct address_space *space, uint32_t addr, uint8_t data);
void as_write16(struct address_space *space, uint32_t addr, uint16_t data);
void as_write32(struct address_space *space, uint32_t addr, uint32_t data);

void as_memcpy_to_guest(struct address_space *space, uint32_t virtual_dest,
                        const void *ptr, int size);
void as_memcpy_to_host(struct address_space *space, void *ptr,
                       uint32_t virtual_src, int size);
void as_memcpy(struct address_space *space, uint32_t virtual_dest,
               uint32_t virtual_src, int size);

#endif
