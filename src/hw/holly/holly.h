#ifndef HOLLY_H
#define HOLLY_H

#include <stdint.h>
#include "hw/holly/holly_types.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dreamcast_s;
struct gdrom_s;
struct maple_s;
struct sh4_s;

typedef struct holly_s {
  device_t base;

  struct gdrom_s *gdrom;
  struct maple_s *maple;
  struct sh4_s *sh4;

  uint32_t reg[NUM_HOLLY_REGS];
  void *reg_data[NUM_HOLLY_REGS];
  reg_read_cb reg_read[NUM_HOLLY_REGS];
  reg_write_cb reg_write[NUM_HOLLY_REGS];

#define HOLLY_REG(offset, name, default, type) type *name;
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG
} holly_t;

AM_DECLARE(holly_reg_map);

holly_t *holly_create(struct dreamcast_s *dc);
void holly_destroy(holly_t *hl);
void holly_raise_interrupt(holly_t *hl, holly_interrupt_t intr);
void holly_clear_interrupt(holly_t *hl, holly_interrupt_t intr);

#ifdef __cplusplus
}
#endif

#endif
