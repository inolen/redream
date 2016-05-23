#ifndef SH4_H
#define SH4_H

#include "hw/sh4/sh4_types.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "jit/frontend/sh4/sh4_context.h"

#ifdef __cplusplus
extern "C" {
#endif

struct SH4Test;

struct dreamcast_s;

static const int MAX_MIPS_SAMPLES = 10;

typedef struct {
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
} sh4_dtr_t;

typedef struct {
  bool show;
  // std::chrono::high_resolution_clock::time_point last_mips_time;
  float mips[MAX_MIPS_SAMPLES];
  int num_mips;
} sh4_perf_t;

typedef struct sh4_s {
  device_t base;

  struct scheduler_s *scheduler;
  address_space_t *space;

  struct sh4_cache_s *code_cache;

  sh4_context_t ctx;
  uint8_t cache[0x2000];  // 8kb cache
  // std::map<uint32_t, uint16_t> breakpoints;

  uint32_t reg[NUM_SH4_REGS];
  void *reg_data[NUM_SH4_REGS];
  reg_read_cb reg_read[NUM_SH4_REGS];
  reg_write_cb reg_write[NUM_SH4_REGS];

#define SH4_REG(addr, name, default, type) type *name;
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

  sh4_interrupt_t sorted_interrupts[NUM_SH_INTERRUPTS];
  uint64_t sort_id[NUM_SH_INTERRUPTS];
  uint64_t priority_mask[16];
  uint64_t requested_interrupts;
  uint64_t pending_interrupts;

  struct timer_s *tmu_timers[3];

  sh4_perf_t perf;
} sh4_t;

AM_DECLARE(sh4_data_map);

struct sh4_s *sh4_create(struct dreamcast_s *dc);
void sh4_destroy(struct sh4_s *sh);

void sh4_set_pc(sh4_t *sh4, uint32_t pc);
void sh4_run(sh4_t *sh4, int64_t ns);
void sh4_raise_interrupt(struct sh4_s *sh, sh4_interrupt_t intr);
void sh4_clear_interrupt(struct sh4_s *sh, sh4_interrupt_t intr);
void sh4_ddt(struct sh4_s *sh, sh4_dtr_t *dtr);

#ifdef __cplusplus
}
#endif

#endif
