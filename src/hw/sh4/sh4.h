#ifndef SH4_H
#define SH4_H

#include "core/profiler.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/sh4/sh4_types.h"
#include "jit/frontend/sh4/sh4_context.h"
#include "jit/jit.h"

struct dreamcast;
struct jit_frontend;
struct jit_backend;

#define SH4_CLOCK_FREQ INT64_C(200000000)

struct sh4_dtr {
  int channel;
  /*
   * when rw is true, addr is the dst address
   * when rw is false, addr is the src address
   */
  bool rw;
  /*
   * when data is non-null, a single address mode transfer is performed between
   * the external device memory at data, and the memory at addr for
   * when data is null, a dual address mode transfer is performed between addr
   * and SARn / DARn
   */
  uint8_t *data;
  uint32_t addr;
  /*
   * size is only valid for single address mode transfers, dual address mode
   * transfers honor DMATCR
   */
  int size;
};

struct sh4 {
  struct device;

  struct sh4_ctx ctx;
  uint32_t reg[NUM_SH4_REGS];
#define SH4_REG(addr, name, default, type) type *name;
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

  /* jit */
  struct jit *jit;
  struct jit_frontend *frontend;
  struct jit_backend *backend;

  /* intc */
  enum sh4_interrupt sorted_interrupts[NUM_SH_INTERRUPTS];
  uint64_t sort_id[NUM_SH_INTERRUPTS];
  uint64_t priority_mask[16];
  uint64_t requested_interrupts;
  /* pending interrupts moved to context for fast jit access */

  /* tmu */
  struct timer *tmu_timers[3];
};

extern struct reg_cb sh4_cb[NUM_SH4_REGS];

DECLARE_PROF_STAT(sh4_instrs);

AM_DECLARE(sh4_data_map);

void sh4_ccn_prefetch(void *data, uint64_t addr);
uint32_t sh4_ccn_cache_read(struct sh4 *sh4, uint32_t addr, uint32_t data_mask);
void sh4_ccn_cache_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                         uint32_t data_mask);
uint32_t sh4_ccn_sq_read(struct sh4 *sh4, uint32_t addr, uint32_t data_mask);
void sh4_ccn_sq_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                      uint32_t data_mask);

void sh4_dmac_ddt(struct sh4 *sh, struct sh4_dtr *dtr);

void sh4_intc_update_pending(struct sh4 *sh4);
int sh4_intc_check_pending(struct sh4 *sh4);
void sh4_intc_reprioritize(struct sh4 *sh4);

struct sh4 *sh4_create(struct dreamcast *dc);
void sh4_destroy(struct sh4 *sh);
void sh4_reset(struct sh4 *sh4, uint32_t pc);
void sh4_raise_interrupt(struct sh4 *sh, enum sh4_interrupt intr);
void sh4_clear_interrupt(struct sh4 *sh, enum sh4_interrupt intr);
void sh4_sr_updated(void *data, uint64_t old_sr);
void sh4_fpscr_updated(void *data, uint64_t old_fpscr);

#endif
