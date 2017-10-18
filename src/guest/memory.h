#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include "core/memory.h"

struct dreamcast;
struct memory;

/*
 * mmio callbacks and helpers
 */
#define DATA_SIZE() (ctz64((uint64_t)mask + 1) >> 3)
#define READ_DATA(ptr) ((*(uint32_t *)(ptr)) & mask)
#define WRITE_DATA(ptr) \
  (*(uint32_t *)(ptr) = (*(uint32_t *)(ptr) & ~mask) | (data & mask))

typedef uint32_t (*mmio_read_cb)(void *, uint32_t, uint32_t);
typedef void (*mmio_write_cb)(void *, uint32_t, uint32_t, uint32_t);
typedef void (*mmio_read_string_cb)(void *, uint8_t *, uint32_t, int);
typedef void (*mmio_write_string_cb)(void *, uint32_t, const uint8_t *, int);

#define DECLARE_ADDRESS_SPACE(space)                                       \
  uint8_t *space##_base(struct memory *mem);                               \
  uint8_t space##_read8(struct memory *mem, uint32_t addr);                \
  uint16_t space##_read16(struct memory *mem, uint32_t addr);              \
  uint32_t space##_read32(struct memory *mem, uint32_t addr);              \
  void space##_write8(struct memory *mem, uint32_t addr, uint8_t data);    \
  void space##_write16(struct memory *mem, uint32_t addr, uint16_t data);  \
  void space##_write32(struct memory *mem, uint32_t addr, uint32_t data);  \
  void space##_memcpy_to_guest(struct memory *mem, uint32_t dst,           \
                               const void *ptr, int size);                 \
  void space##_memcpy_to_host(struct memory *mem, void *ptr, uint32_t src, \
                              int size);                                   \
  void space##_memcpy(struct memory *mem, uint32_t dst, uint32_t src,      \
                      int size);                                           \
  void space##_lookup(struct memory *mem, uint32_t addr, void **userdata,  \
                      uint8_t **ptr, mmio_read_cb *read,                   \
                      mmio_write_cb *write);

DECLARE_ADDRESS_SPACE(sh4);
DECLARE_ADDRESS_SPACE(arm7);

struct memory *mem_create(struct dreamcast *dc);
void mem_destroy(struct memory *mem);

int mem_init(struct memory *mem);

uint8_t *mem_ram(struct memory *mem, uint32_t offset);
uint8_t *mem_aram(struct memory *mem, uint32_t offset);
uint8_t *mem_vram(struct memory *mem, uint32_t offset);

#endif
