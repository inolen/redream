#ifndef SH4_H
#define SH4_H

#include "guest/dreamcast.h"
#include "guest/sh4/sh4_ccn.h"
#include "guest/sh4/sh4_dbg.h"
#include "guest/sh4/sh4_dmac.h"
#include "guest/sh4/sh4_intc.h"
#include "guest/sh4/sh4_mem.h"
#include "guest/sh4/sh4_mmu.h"
#include "guest/sh4/sh4_scif.h"
#include "guest/sh4/sh4_tmu.h"
#include "guest/sh4/sh4_types.h"
#include "jit/frontend/sh4/sh4_guest.h"
#include "jit/jit.h"

struct dreamcast;
struct jit;
struct jit_backend;
struct jit_frontend;
struct jit_guest;

#define SH4_CLOCK_FREQ INT64_C(200000000)

typedef int (*sh4_exception_handler_cb)(void *, enum sh4_exception);

struct sh4 {
  struct device;

  struct sh4_context ctx;
  uint32_t reg[SH4_NUM_REGS];
#define SH4_REG(addr, name, default, type) type *name;
#include "guest/sh4/sh4_regs.inc"
#undef SH4_REG

  /* custom exception handler */
  sh4_exception_handler_cb exc_handler;
  void *exc_handler_data;

  /* jit */
  struct jit *jit;
  struct jit_guest *guest;
  struct jit_frontend *frontend;
  struct jit_backend *backend;

  /* dbg */
  int log_regs;
  int tmu_stats;
  struct list breakpoints;

  /* ccn */
  uint32_t sq[2][8];

  /* intc */
  enum sh4_interrupt sorted_interrupts[SH4_NUM_INTERRUPTS];
  uint64_t sort_id[SH4_NUM_INTERRUPTS];
  uint64_t priority_mask[16];
  uint64_t requested_interrupts;
  /* pending interrupts moved to context for fast jit access */

  /* mmu */
  uint32_t utlb_sq_map[64];
  struct sh4_tlb_entry utlb[64];

  /* scif */
  uint32_t SCFSR2_last_read;
  struct sh4_scif_fifo receive_fifo;
  struct sh4_scif_fifo transmit_fifo;

  /* tmu */
  struct timer *tmu_timers[3];
};

extern struct reg_cb sh4_cb[SH4_NUM_REGS];
extern struct sh4_exception_info sh4_exceptions[SH4_NUM_EXCEPTIONS];
extern struct sh4_interrupt_info sh4_interrupts[SH4_NUM_INTERRUPTS];

struct sh4 *sh4_create(struct dreamcast *dc);
void sh4_destroy(struct sh4 *sh4);
void sh4_debug_menu(struct sh4 *sh4);
void sh4_reset(struct sh4 *sh4, uint32_t pc);

void sh4_set_exception_handler(struct sh4 *sh4,
                               sh4_exception_handler_cb handler, void *data);

void sh4_raise_interrupt(struct sh4 *sh4, enum sh4_interrupt intr);
void sh4_clear_interrupt(struct sh4 *sh4, enum sh4_interrupt intr);

#endif
