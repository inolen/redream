#ifndef SH4_H
#define SH4_H

#include "core/profiler.h"
#include "guest/dreamcast.h"
#include "guest/memory.h"
#include "guest/sh4/sh4_types.h"
#include "jit/frontend/sh4/sh4_guest.h"
#include "jit/jit.h"

struct dreamcast;
struct jit;
struct jit_backend;
struct jit_frontend;
struct sh4_guest;

#define SH4_CLOCK_FREQ INT64_C(200000000)

enum {
  SH4_DMA_FROM_ADDR,
  SH4_DMA_TO_ADDR,
};

typedef int (*sh4_exception_handler_cb)(void *, enum sh4_exception);

struct sh4_dtr {
  int channel;
  int dir;
  /* when data is non-null, a single address mode transfer is performed between
     the external device memory at data, and the memory at addr

     when data is null, a dual address mode transfer is performed between addr
     and SARn / DARn */
  uint8_t *data;
  uint32_t addr;
  /* size is only valid for single address mode transfers, dual address mode
     transfers honor DMATCR */
  int size;
};

struct sh4_tlb_entry {
  union pteh hi;
  union ptel lo;
};

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
  struct sh4_guest *guest;
  struct jit_frontend *frontend;
  struct jit_backend *backend;

  /* dbg */
  struct list breakpoints;

  /* intc */
  enum sh4_interrupt sorted_interrupts[SH4_NUM_INTERRUPTS];
  uint64_t sort_id[SH4_NUM_INTERRUPTS];
  uint64_t priority_mask[16];
  uint64_t requested_interrupts;
  /* pending interrupts moved to context for fast jit access */

  /* mmu */
  uint32_t utlb_sq_map[64];
  struct sh4_tlb_entry utlb[64];

  /* tmu */
  struct timer *tmu_timers[3];
};

extern struct reg_cb sh4_cb[SH4_NUM_REGS];
extern struct sh4_exception_info sh4_exceptions[SH4_NUM_EXCEPTIONS];
extern struct sh4_interrupt_info sh4_interrupts[SH4_NUM_INTERRUPTS];

DECLARE_COUNTER(sh4_instrs);

AM_DECLARE(sh4_data_map);

/* ccn */
void sh4_ccn_pref(struct sh4 *sh4, uint32_t addr);
uint32_t sh4_ccn_cache_read(struct sh4 *sh4, uint32_t addr, uint32_t data_mask);
void sh4_ccn_cache_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                         uint32_t data_mask);
uint32_t sh4_ccn_sq_read(struct sh4 *sh4, uint32_t addr, uint32_t data_mask);
void sh4_ccn_sq_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                      uint32_t data_mask);
uint32_t sh4_ccn_icache_read(struct sh4 *sh4, uint32_t addr,
                             uint32_t data_mask);
void sh4_ccn_icache_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                          uint32_t data_mask);
uint32_t sh4_ccn_ocache_read(struct sh4 *sh4, uint32_t addr,
                             uint32_t data_mask);
void sh4_ccn_ocache_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                          uint32_t data_mask);

/* dbg */
int sh4_dbg_num_registers(struct device *dev);
void sh4_dbg_step(struct device *dev);
void sh4_dbg_add_breakpoint(struct device *dev, int type, uint32_t addr);
void sh4_dbg_remove_breakpoint(struct device *dev, int type, uint32_t addr);
void sh4_dbg_read_memory(struct device *dev, uint32_t addr, uint8_t *buffer,
                         int size);
void sh4_dbg_read_register(struct device *dev, int n, uint64_t *value,
                           int *size);
int sh4_dbg_invalid_instr(struct sh4 *sh4);

void sh4_dmac_ddt(struct sh4 *sh, struct sh4_dtr *dtr);

/* intc */
void sh4_intc_update_pending(struct sh4 *sh4);
void sh4_intc_reprioritize(struct sh4 *sh4);

/* mmu */
void sh4_mmu_ltlb(struct sh4 *sh4);
uint32_t sh4_mmu_itlb_read(struct sh4 *sh4, uint32_t addr, uint32_t data_mask);
uint32_t sh4_mmu_utlb_read(struct sh4 *sh4, uint32_t addr, uint32_t data_mask);
void sh4_mmu_itlb_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                        uint32_t data_mask);
void sh4_mmu_utlb_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                        uint32_t data_mask);

struct sh4 *sh4_create(struct dreamcast *dc);
void sh4_destroy(struct sh4 *sh4);
void sh4_debug_menu(struct sh4 *sh4);
void sh4_reset(struct sh4 *sh4, uint32_t pc);

void sh4_set_exception_handler(struct sh4 *sh4,
                               sh4_exception_handler_cb handler, void *data);

void sh4_raise_interrupt(struct sh4 *sh4, enum sh4_interrupt intr);
void sh4_clear_interrupt(struct sh4 *sh4, enum sh4_interrupt intr);

#endif
