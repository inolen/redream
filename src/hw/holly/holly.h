#ifndef HOLLY_H
#define HOLLY_H

#include <stdint.h>
#include "hw/dreamcast.h"
#include "hw/holly/holly_types.h"
#include "hw/memory.h"

struct dreamcast;
struct gdrom;
struct maple;
struct sh4;

struct holly {
  struct device base;

  struct gdrom *gdrom;
  struct maple *maple;
  struct sh4 *sh4;

  uint32_t reg[NUM_HOLLY_REGS];
  void *reg_data[NUM_HOLLY_REGS];
  reg_read_cb reg_read[NUM_HOLLY_REGS];
  reg_write_cb reg_write[NUM_HOLLY_REGS];

#define HOLLY_REG(offset, name, default, type) type *name;
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG
};

void holly_raise_interrupt(struct holly *hl, holly_interrupt_t intr);
void holly_clear_interrupt(struct holly *hl, holly_interrupt_t intr);

struct holly *holly_create(struct dreamcast *dc);
void holly_destroy(struct holly *hl);

AM_DECLARE(holly_reg_map);

#endif
