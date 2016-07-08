#ifndef SH4_H
#define SH4_H

#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/sh4/sh4_types.h"
#include "jit/backend/backend.h"
#include "jit/frontend/sh4/sh4_context.h"

struct dreamcast;

#define MAX_MIPS_SAMPLES 10

struct sh4_dtr {
  int channel;
  // when rw is true, addr is the dst address
  // when rw is false, addr is the src address
  bool rw;
  // when data is non-null, a single address mode transfer is performed between
  // the external device memory at data, and the memory at addr for
  // when data is null, a dual address mode transfer is performed between addr
  // and SARn / DARn
  uint8_t *data;
  uint32_t addr;
  // size is only valid for single address mode transfers, dual address mode
  // transfers honor DMATCR
  int size;
};

struct sh4_perf {
  bool show;
  int64_t last_mips_time;
  int mips;
};

struct sh4 {
  struct device base;
  struct scheduler *scheduler;
  struct address_space *space;

  struct jit_memory_interface memory_if;
  struct sh4_cache *code_cache;
  struct sh4_ctx ctx;
  uint8_t cache[0x2000];  // 8kb cache
  // std::map<uint32_t, uint16_t> breakpoints;

  uint32_t reg[NUM_SH4_REGS];
  void *reg_data[NUM_SH4_REGS];
  reg_read_cb reg_read[NUM_SH4_REGS];
  reg_write_cb reg_write[NUM_SH4_REGS];

#define SH4_REG(addr, name, default, type) type *name;
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

  enum sh4_interrupt sorted_interrupts[NUM_SH_INTERRUPTS];
  uint64_t sort_id[NUM_SH_INTERRUPTS];
  uint64_t priority_mask[16];
  uint64_t requested_interrupts;
  uint64_t pending_interrupts;

  struct timer *tmu_timers[3];

  struct sh4_perf perf;
};

void sh4_set_pc(struct sh4 *sh4, uint32_t pc);
void sh4_raise_interrupt(struct sh4 *sh, enum sh4_interrupt intr);
void sh4_clear_interrupt(struct sh4 *sh, enum sh4_interrupt intr);
void sh4_ddt(struct sh4 *sh, struct sh4_dtr *dtr);

struct sh4 *sh4_create(struct dreamcast *dc);
void sh4_destroy(struct sh4 *sh);

AM_DECLARE(sh4_data_map);

#endif
