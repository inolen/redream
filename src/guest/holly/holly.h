#ifndef HOLLY_H
#define HOLLY_H

#include "guest/dreamcast.h"
#include "guest/holly/holly_types.h"

struct gdrom;
struct maple;
struct sh4;

#define HOLLY_G2_NUM_CHAN 4
#define HOLLY_G2_NUM_REGS 8

struct holly_g2_dma {
  uint32_t dst;
  uint32_t src;
  int restart;
  int len;
};

struct holly {
  struct device;
  uint32_t reg[NUM_HOLLY_REGS];

#define HOLLY_REG(offset, name, default, type) type *name;
#include "guest/holly/holly_regs.inc"
#undef HOLLY_REG

  struct holly_g2_dma dma[HOLLY_G2_NUM_CHAN];

  /* debug */
  int log_regs;
};

extern struct reg_cb holly_cb[NUM_HOLLY_REGS];

struct holly *holly_create(struct dreamcast *dc);
void holly_destroy(struct holly *hl);

void holly_debug_menu(struct holly *hl);

uint32_t holly_reg_read(struct holly *hl, uint32_t addr, uint32_t mask);
void holly_reg_write(struct holly *hl, uint32_t addr, uint32_t data,
                     uint32_t mask);

void holly_raise_interrupt(struct holly *hl, holly_interrupt_t intr);
void holly_clear_interrupt(struct holly *hl, holly_interrupt_t intr);

#endif
