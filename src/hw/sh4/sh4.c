#include "hw/sh4/sh4.h"
#include "core/math.h"
#include "core/profiler.h"
#include "core/string.h"
#include "hw/aica/aica.h"
#include "hw/debugger.h"
#include "hw/dreamcast.h"
#include "hw/holly/g2.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr.h"
#include "hw/holly/ta.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4_code_cache.h"
#include "jit/frontend/sh4/sh4_analyze.h"
#include "sys/time.h"
#include "ui/nuklear.h"

#define SH4_CLOCK_FREQ INT64_C(200000000)

struct sh4_interrupt_info {
  int intevt, default_priority, ipr, ipr_shift;
};

static struct sh4_interrupt_info sh4_interrupts[NUM_SH_INTERRUPTS] = {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) \
  {intevt, pri, ipr, ipr_shift},
#include "hw/sh4/sh4_int.inc"
#undef SH4_INT
};

static struct reg_cb sh4_cb[NUM_SH4_REGS];

static struct sh4 *g_sh4;

static void sh4_sr_updated(struct sh4_ctx *ctx, uint64_t old_sr);

//
// TMU
//
static const int64_t PERIPHERAL_CLOCK_FREQ = SH4_CLOCK_FREQ >> 2;
static const int PERIPHERAL_SCALE[] = {2, 4, 6, 8, 10, 0, 0, 0};

#define TSTR(n) (*sh4->TSTR & (1 << n))
#define TCOR(n) (n == 0 ? sh4->TCOR0 : n == 1 ? sh4->TCOR1 : sh4->TCOR2)
#define TCNT(n) (n == 0 ? sh4->TCNT0 : n == 1 ? sh4->TCNT1 : sh4->TCNT2)
#define TCR(n) (n == 0 ? sh4->TCR0 : n == 1 ? sh4->TCR1 : sh4->TCR2)
#define TUNI(n) \
  (n == 0 ? SH4_INTC_TUNI0 : n == 1 ? SH4_INTC_TUNI1 : SH4_INTC_TUNI2)

static void sh4_tmu_reschedule(struct sh4 *sh4, int n, uint32_t tcnt,
                               uint32_t tcr);

static uint32_t sh4_tmu_tcnt(struct sh4 *sh4, int n) {
  // TCNT values aren't updated in real time. if a timer is enabled, query
  // the scheduler to figure out how many cycles are remaining for the given
  // timer
  if (!TSTR(n)) {
    return *TCNT(n);
  }

  // FIXME should the number of SH4 cycles that've been executed be considered
  // here? this would prevent an entire SH4 slice from just busy waiting on
  // this to change

  struct timer *timer = sh4->tmu_timers[n];
  uint32_t tcr = *TCR(n);

  int64_t freq = PERIPHERAL_CLOCK_FREQ >> PERIPHERAL_SCALE[tcr & 7];
  int64_t remaining = scheduler_remaining_time(sh4->scheduler, timer);
  int64_t cycles = NANO_TO_CYCLES(remaining, freq);

  return (uint32_t)cycles;
}

static void sh4_tmu_expire(struct sh4 *sh4, int n) {
  uint32_t *tcor = TCOR(n);
  uint32_t *tcnt = TCNT(n);
  uint32_t *tcr = TCR(n);

  LOG_INFO("sh4_tmu_expire");

  // timer expired, set the underflow flag
  *tcr |= 0x100;

  // if interrupt generation on underflow is enabled, do so
  if (*tcr & 0x20) {
    sh4_raise_interrupt(sh4, TUNI(n));
  }

  // reset TCNT with the value from TCOR
  *tcnt = *tcor;

  // reschedule the timer with the new count
  sh4_tmu_reschedule(sh4, n, *tcnt, *tcr);
}

static void sh4_tmu_expire_0(void *data) {
  sh4_tmu_expire(data, 0);
}

static void sh4_tmu_expire_1(void *data) {
  sh4_tmu_expire(data, 1);
}

static void sh4_tmu_expire_2(void *data) {
  sh4_tmu_expire(data, 2);
}

static void sh4_tmu_reschedule(struct sh4 *sh4, int n, uint32_t tcnt,
                               uint32_t tcr) {
  struct timer **timer = &sh4->tmu_timers[n];

  int64_t freq = PERIPHERAL_CLOCK_FREQ >> PERIPHERAL_SCALE[tcr & 7];
  int64_t cycles = (int64_t)tcnt;
  int64_t remaining = CYCLES_TO_NANO(cycles, freq);

  if (*timer) {
    scheduler_cancel_timer(sh4->scheduler, *timer);
    *timer = NULL;
  }

  timer_cb cb = (n == 0 ? &sh4_tmu_expire_0 : n == 1 ? &sh4_tmu_expire_1
                                                     : &sh4_tmu_expire_2);
  *timer = scheduler_start_timer(sh4->scheduler, cb, sh4, remaining);
}

static void sh4_tmu_update_tstr(struct sh4 *sh4) {
  for (int i = 0; i < 3; i++) {
    struct timer **timer = &sh4->tmu_timers[i];

    if (TSTR(i)) {
      // schedule the timer if not already started
      if (!*timer) {
        sh4_tmu_reschedule(sh4, i, *TCNT(i), *TCR(i));
      }
    } else if (*timer) {
      // disable the timer
      scheduler_cancel_timer(sh4->scheduler, *timer);
      *timer = NULL;
    }
  }
}

static void sh4_tmu_update_tcr(struct sh4 *sh4, uint32_t n) {
  if (TSTR(n)) {
    // timer is already scheduled, reschedule it with the current cycle
    // count, but the new TCR value
    sh4_tmu_reschedule(sh4, n, sh4_tmu_tcnt(sh4, n), *TCR(n));
  }

  // if the timer no longer cares about underflow interrupts, unrequest
  if (!(*TCR(n) & 0x20) || !(*TCR(n) & 0x100)) {
    sh4_clear_interrupt(sh4, TUNI(n));
  }
}

static void sh4_tmu_update_tcnt(struct sh4 *sh4, uint32_t n) {
  if (TSTR(n)) {
    sh4_tmu_reschedule(sh4, n, *TCNT(n), *TCR(n));
  }
}

//
// INTC
//
static void sh4_intc_update_pending(struct sh4 *sh4) {
  int min_priority = (sh4->ctx.sr & I) >> 4;
  uint64_t priority_mask =
      (sh4->ctx.sr & BL) ? 0 : ~sh4->priority_mask[min_priority];
  sh4->pending_interrupts = sh4->requested_interrupts & priority_mask;
}

static void sh4_intc_check_pending(struct sh4 *sh4) {
  if (!sh4->pending_interrupts) {
    return;
  }

  // process the highest priority in the pending vector
  int n = 63 - clz64(sh4->pending_interrupts);
  enum sh4_interrupt intr = sh4->sorted_interrupts[n];
  struct sh4_interrupt_info *int_info = &sh4_interrupts[intr];

  *sh4->INTEVT = int_info->intevt;
  sh4->ctx.ssr = sh4->ctx.sr;
  sh4->ctx.spc = sh4->ctx.pc;
  sh4->ctx.sgr = sh4->ctx.r[15];
  sh4->ctx.sr |= (BL | MD | RB);
  sh4->ctx.pc = sh4->ctx.vbr + 0x600;

  sh4_sr_updated(&sh4->ctx, sh4->ctx.ssr);
}

// Generate a sorted set of interrupts based on their priority. These sorted
// ids are used to represent all of the currently requested interrupts as a
// simple bitmask.
static void sh4_intc_reprioritize(struct sh4 *sh4) {
  uint64_t old = sh4->requested_interrupts;
  sh4->requested_interrupts = 0;

  for (int i = 0, n = 0; i < 16; i++) {
    // for even priorities, give precedence to lower id interrupts
    for (int j = NUM_SH_INTERRUPTS - 1; j >= 0; j--) {
      struct sh4_interrupt_info *int_info = &sh4_interrupts[j];

      // get current priority for interrupt
      int priority = int_info->default_priority;
      if (int_info->ipr) {
        uint32_t ipr = sh4->reg[int_info->ipr];
        priority = ((ipr & 0xffff) >> int_info->ipr_shift) & 0xf;
      }

      if (priority != i) {
        continue;
      }

      bool was_requested = old & sh4->sort_id[j];

      sh4->sorted_interrupts[n] = j;
      sh4->sort_id[j] = (uint64_t)1 << n;
      n++;

      if (was_requested) {
        // rerequest with new sorted id
        sh4->requested_interrupts |= sh4->sort_id[j];
      }
    }

    // generate a mask for all interrupts up to the current priority
    sh4->priority_mask[i] = ((uint64_t)1 << n) - 1;
  }

  sh4_intc_update_pending(sh4);
}

//
// DMAC
//
static void sh4_dmac_check(struct sh4 *sh4, int channel) {
  union chcr *chcr = NULL;

  switch (channel) {
    case 0:
      chcr = sh4->CHCR0;
      break;
    case 1:
      chcr = sh4->CHCR1;
      break;
    case 2:
      chcr = sh4->CHCR2;
      break;
    case 3:
      chcr = sh4->CHCR3;
      break;
    default:
      LOG_FATAL("Unexpected DMA channel");
      break;
  }

  CHECK(sh4->DMAOR->DDT || !sh4->DMAOR->DME || !chcr->DE,
        "Non-DDT DMA not supported");
}

//
// CCN
//
static void sh4_ccn_reset(struct sh4 *sh4) {
  // FIXME this isn't right. When the IC is reset a pending flag is set and the
  // cache is actually reset at the end of the current block. However, the docs
  // for the SH4 IC state "After CCR is updated, an instruction that performs
  // data access to the P0, P1, P3, or U0 area should be located at least four
  // instructions after the CCR update instruction. Also, a branch instruction
  // to the P0, P1, P3, or U0 area should be located at least eight instructions
  // after the CCR update instruction."
  LOG_INFO("Reset instruction cache");

  sh4_cache_unlink_blocks(sh4->code_cache);
}

static uint32_t sh4_compile_pc() {
  uint32_t guest_addr = g_sh4->ctx.pc;
  uint8_t *guest_ptr = as_translate(g_sh4->memory_if->space, guest_addr);

  int flags = 0;
  if (g_sh4->ctx.fpscr & PR) {
    flags |= SH4_DOUBLE_PR;
  }
  if (g_sh4->ctx.fpscr & SZ) {
    flags |= SH4_DOUBLE_SZ;
  }

  code_pointer_t code =
      sh4_cache_compile_code(g_sh4->code_cache, guest_addr, guest_ptr, flags);

  return code();
}

static void sh4_invalid_instr(struct sh4_ctx *ctx, uint64_t data) {
  // struct sh4 *self = reinterpret_cast<SH4 *>(ctx->sh4);
  // uint32_t addr = (uint32_t)data;

  // auto it = self->breakpoints.find(addr);
  // CHECK_NE(it, self->breakpoints.end());

  // // force the main loop to break
  // self->ctx.num_cycles = 0;

  // // let the debugger know execution has stopped
  // self->dc->debugger->Trap();
}

static void sh4_prefetch(struct sh4_ctx *ctx, uint64_t data) {
  struct sh4 *sh4 = ctx->sh4;
  uint32_t addr = (uint32_t)data;

  // only concerned about SQ related prefetches
  if (addr < 0xe0000000 || addr > 0xe3ffffff) {
    return;
  }

  // figure out the source and destination
  uint32_t dest = addr & 0x03ffffe0;
  uint32_t sqi = (addr & 0x20) >> 5;
  if (sqi) {
    dest |= (*sh4->QACR1 & 0x1c) << 24;
  } else {
    dest |= (*sh4->QACR0 & 0x1c) << 24;
  }

  // perform the "burst" 32-byte copy
  for (int i = 0; i < 8; i++) {
    as_write32(sh4->memory_if->space, dest, sh4->ctx.sq[sqi][i]);
    dest += 4;
  }
}

static void sh4_swap_gpr_bank(struct sh4 *sh4) {
  for (int s = 0; s < 8; s++) {
    uint32_t tmp = sh4->ctx.r[s];
    sh4->ctx.r[s] = sh4->ctx.ralt[s];
    sh4->ctx.ralt[s] = tmp;
  }
}

static void sh4_sr_updated(struct sh4_ctx *ctx, uint64_t old_sr) {
  struct sh4 *sh4 = ctx->sh4;

  if ((ctx->sr & RB) != (old_sr & RB)) {
    sh4_swap_gpr_bank(sh4);
  }

  if ((ctx->sr & I) != (old_sr & I) || (ctx->sr & BL) != (old_sr & BL)) {
    sh4_intc_update_pending(sh4);
  }
}

static void sh4_swap_fpr_bank(struct sh4 *sh4) {
  for (int s = 0; s <= 15; s++) {
    uint32_t tmp = sh4->ctx.fr[s];
    sh4->ctx.fr[s] = sh4->ctx.xf[s];
    sh4->ctx.xf[s] = tmp;
  }
}

static void sh4_fpscr_updated(struct sh4_ctx *ctx, uint64_t old_fpscr) {
  struct sh4 *sh4 = ctx->sh4;

  if ((ctx->fpscr & FR) != (old_fpscr & FR)) {
    sh4_swap_fpr_bank(sh4);
  }
}

// static int sh4_debug_num_registers() {
//   return 59;
// }

// static void sh4_debug_step() {
//   // invalidate the block for the current pc
//   sh4->code_cache->RemoveBlocks(sh4->ctx.pc);

//   // recompile it with only one instruction and run it
//   uint32_t guest_addr = sh4->ctx.pc;
//   uint8_t *host_addr = space->Translate(guest_addr);
//   int flags = GetCompileFlags() | SH4_SINGLE_INSTR;

//   code_pointer_t code = sh4->code_cache->CompileCode(guest_addr, host_addr,
//   flags);
//   sh4->ctx.pc = code();

//   // let the debugger know we've stopped
//   dc->debugger->Trap();
// }

// static void sh4_debug_add_breakpoint(int type, uint32_t addr) {
//   // save off the original instruction
//   uint16_t instr = space->R16(addr);
//   breakpoints_.insert(std::make_pair(addr, instr));

//   // write out an invalid instruction
//   space->W16(addr, 0);

//   sh4->code_cache->RemoveBlocks(addr);
// }

// static void sh4_debug_remove_breakpoint(int type, uint32_t addr) {
//   // recover the original instruction
//   auto it = breakpoints_.find(addr);
//   CHECK_NE(it, breakpoints_.end());
//   uint16_t instr = it->second;
//   breakpoints_.erase(it);

//   // overwrite the invalid instruction with the original
//   space->W16(addr, instr);

//   sh4->code_cache->RemoveBlocks(addr);
// }

// static void sh4_debug_read_memory(uint32_t addr, uint8_t *buffer, int size) {
//   space->Memcpy(buffer, addr, size);
// }

// void sh4_debug_read_register(int n, uint64_t *value, int *size) {
//   if (n < 16) {
//     *value = sh4->ctx.r[n];
//   } else if (n == 16) {
//     *value = sh4->ctx.pc;
//   } else if (n == 17) {
//     *value = sh4->ctx.pr;
//   } else if (n == 18) {
//     *value = sh4->ctx.gbr;
//   } else if (n == 19) {
//     *value = sh4->ctx.vbr;
//   } else if (n == 20) {
//     *value = sh4->ctx.mach;
//   } else if (n == 21) {
//     *value = sh4->ctx.macl;
//   } else if (n == 22) {
//     *value = sh4->ctx.sr;
//   } else if (n == 23) {
//     *value = sh4->ctx.fpul;
//   } else if (n == 24) {
//     *value = sh4->ctx.fpscr;
//   } else if (n < 41) {
//     *value = sh4->ctx.fr[n - 25];
//   } else if (n == 41) {
//     *value = sh4->ctx.ssr;
//   } else if (n == 42) {
//     *value = sh4->ctx.spc;
//   } else if (n < 51) {
//     uint32_t *b0 = (sh4->ctx.sr & RB) ? sh4->ctx.ralt : sh4->ctx.r;
//     *value = b0[n - 43];
//   } else if (n < 59) {
//     uint32_t *b1 = (sh4->ctx.sr & RB) ? sh4->ctx.r : sh4->ctx.ralt;
//     *value = b1[n - 51];
//   }

//   *size = 4;
// }

#define define_reg_read(name, type)                            \
  static type sh4_reg_##name(struct sh4 *sh4, uint32_t addr) { \
    uint32_t offset = SH4_REG_OFFSET(addr);                    \
    reg_read_cb read = sh4_cb[offset].read;                    \
    if (read) {                                                \
      return read(sh4->dc);                                    \
    }                                                          \
    return (type)sh4->reg[offset];                             \
  }

define_reg_read(r8, uint8_t);
define_reg_read(r16, uint16_t);
define_reg_read(r32, uint32_t);

#define define_reg_write(name, type)                                       \
  static void sh4_reg_##name(struct sh4 *sh4, uint32_t addr, type value) { \
    uint32_t offset = SH4_REG_OFFSET(addr);                                \
    reg_write_cb write = sh4_cb[offset].write;                             \
    if (write) {                                                           \
      write(sh4->dc, value);                                               \
      return;                                                              \
    }                                                                      \
    sh4->reg[offset] = (uint32_t)value;                                    \
  }

define_reg_write(w8, uint8_t);
define_reg_write(w16, uint16_t);
define_reg_write(w32, uint32_t);

// with OIX, bit 25, rather than bit 13, determines which 4kb bank to use
#define CACHE_OFFSET(addr, OIX) \
  ((OIX ? ((addr & 0x2000000) >> 13) : ((addr & 0x2000) >> 1)) | (addr & 0xfff))

#define define_cache_read(name, type)                            \
  static type sh4_cache_##name(struct sh4 *sh4, uint32_t addr) { \
    CHECK_EQ(sh4->CCR->ORA, 1u);                                 \
    addr = CACHE_OFFSET(addr, sh4->CCR->OIX);                    \
    return *(type *)&sh4->cache[addr];                           \
  }

define_cache_read(r8, uint8_t);
define_cache_read(r16, uint16_t);
define_cache_read(r32, uint32_t);
define_cache_read(r64, uint64_t);

#define define_cache_write(name, type)                                       \
  static void sh4_cache_##name(struct sh4 *sh4, uint32_t addr, type value) { \
    CHECK_EQ(sh4->CCR->ORA, 1u);                                             \
    addr = CACHE_OFFSET(addr, sh4->CCR->OIX);                                \
    *(type *)&sh4->cache[addr] = value;                                      \
  }

define_cache_write(w8, uint8_t);
define_cache_write(w16, uint16_t);
define_cache_write(w32, uint32_t);
define_cache_write(w64, uint64_t);

#define define_sq_read(name, type)                            \
  static type sh4_sq_##name(struct sh4 *sh4, uint32_t addr) { \
    uint32_t sqi = (addr & 0x20) >> 5;                        \
    unsigned idx = (addr & 0x1c) >> 2;                        \
    return (type)sh4->ctx.sq[sqi][idx];                       \
  }

define_sq_read(r8, uint8_t);
define_sq_read(r16, uint16_t);
define_sq_read(r32, uint32_t);

#define define_sq_write(name, type)                                       \
  static void sh4_sq_##name(struct sh4 *sh4, uint32_t addr, type value) { \
    uint32_t sqi = (addr & 0x20) >> 5;                                    \
    uint32_t idx = (addr & 0x1c) >> 2;                                    \
    sh4->ctx.sq[sqi][idx] = (uint32_t)value;                              \
  }

define_sq_write(w8, uint8_t);
define_sq_write(w16, uint16_t);
define_sq_write(w32, uint32_t);

static bool sh4_init(struct device *dev) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct dreamcast *dc = sh4->dc;

  sh4->jit_if = (struct jit_memory_interface){&sh4->ctx,
                                              sh4->memory_if->space->base,
                                              sh4->memory_if->space,
                                              &as_read8,
                                              &as_read16,
                                              &as_read32,
                                              &as_read64,
                                              &as_write8,
                                              &as_write16,
                                              &as_write32,
                                              &as_write64};
  sh4->code_cache = sh4_cache_create(&sh4->jit_if, &sh4_compile_pc);

  // initialize context
  sh4->ctx.sh4 = sh4;
  sh4->ctx.InvalidInstruction = &sh4_invalid_instr;
  sh4->ctx.Prefetch = &sh4_prefetch;
  sh4->ctx.SRUpdated = &sh4_sr_updated;
  sh4->ctx.FPSCRUpdated = &sh4_fpscr_updated;
  sh4->ctx.pc = 0xa0000000;
  sh4->ctx.r[15] = 0x8d000000;
  sh4->ctx.pr = 0x0;
  sh4->ctx.sr = 0x700000f0;
  sh4->ctx.fpscr = 0x00040001;

// initialize registers
#define SH4_REG(addr, name, default, type) \
  sh4->reg[name] = default;                \
  sh4->name = (type *)&sh4->reg[name];
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

  // reset interrupts
  sh4_intc_reprioritize(sh4);

  return true;
}

static void sh4_paint_debug_menu(struct device *dev, struct nk_context *ctx) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct sh4_perf *perf = &sh4->perf;

  if (nk_tree_push(ctx, NK_TREE_TAB, "sh4", NK_MINIMIZED)) {
    nk_value_int(ctx, "mips", perf->mips);
    nk_tree_pop(ctx);
  }

  /*if (perf->show) {
    struct nk_panel layout;
    struct nk_rect bounds = {440.0f, 20.0f, 200.0f, 20.0f};

    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);
    ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, &layout, "sh4 perf", bounds, NK_WINDOW_NO_SCROLLBAR)) {
      nk_layout_row_static(ctx, bounds.h, bounds.w, 1);

      if (nk_chart_begin(ctx, NK_CHART_LINES, MAX_MIPS_SAMPLES, 0.0f, 400.0f)) {
        for (int i = 0; i < MAX_MIPS_SAMPLES; i++) {
          nk_flags res = nk_chart_push(ctx, perf->mips[i]);
        }

        nk_chart_end(ctx);
      }
    }
    nk_end(ctx);
  }*/
}

void sh4_set_pc(struct sh4 *sh4, uint32_t pc) {
  sh4->ctx.pc = pc;
}

static void sh4_run_inner(struct device *dev, int64_t ns) {
  struct sh4 *sh4 = (struct sh4 *)dev;

  // execute at least 1 cycle. the tests rely on this to step block by block
  int64_t cycles = MAX(NANO_TO_CYCLES(ns, SH4_CLOCK_FREQ), INT64_C(1));

  // each block's epilog will decrement the remaining cycles as they run
  sh4->ctx.num_cycles = (int)cycles;

  while (sh4->ctx.num_cycles > 0) {
    code_pointer_t code = sh4_cache_get_code(sh4->code_cache, sh4->ctx.pc);
    sh4->ctx.pc = code();

    sh4_intc_check_pending(sh4);
  }

  // track mips
  int64_t now = time_nanoseconds();
  int64_t next_time = sh4->perf.last_mips_time + NS_PER_SEC;

  if (now > next_time) {
    // convert total number of instructions / nanoseconds delta into millions
    // of instructions per second
    float num_instrs_millions = sh4->ctx.num_instrs / 1000000.0f;
    int64_t delta_ns = now - sh4->perf.last_mips_time;
    float delta_s = delta_ns / 1000000000.0f;
    sh4->perf.mips = (int)(num_instrs_millions / delta_s);

    // reset state
    sh4->perf.last_mips_time = now;
    sh4->ctx.num_instrs = 0;
  }
}

void sh4_run(struct device *dev, int64_t ns) {
  prof_enter("sh4_run");

  sh4_run_inner(dev, ns);

  prof_leave();
}

void sh4_raise_interrupt(struct sh4 *sh4, enum sh4_interrupt intr) {
  sh4->requested_interrupts |= sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

void sh4_clear_interrupt(struct sh4 *sh4, enum sh4_interrupt intr) {
  sh4->requested_interrupts &= ~sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

void sh4_ddt(struct sh4 *sh4, struct sh4_dtr *dtr) {
  if (dtr->data) {
    // single address mode transfer
    if (dtr->rw) {
      as_memcpy_to_guest(sh4->memory_if->space, dtr->addr, dtr->data,
                         dtr->size);
    } else {
      as_memcpy_to_host(sh4->memory_if->space, dtr->data, dtr->addr, dtr->size);
    }
  } else {
    // dual address mode transfer
    // NOTE this should be made asynchronous, at which point the significance
    // of the registers / interrupts should be more obvious
    uint32_t *sar;
    uint32_t *dar;
    uint32_t *dmatcr;
    union chcr *chcr;
    enum sh4_interrupt dmte;

    switch (dtr->channel) {
      case 0:
        sar = sh4->SAR0;
        dar = sh4->DAR0;
        dmatcr = sh4->DMATCR0;
        chcr = sh4->CHCR0;
        dmte = SH4_INTC_DMTE0;
        break;
      case 1:
        sar = sh4->SAR1;
        dar = sh4->DAR1;
        dmatcr = sh4->DMATCR1;
        chcr = sh4->CHCR1;
        dmte = SH4_INTC_DMTE1;
        break;
      case 2:
        sar = sh4->SAR2;
        dar = sh4->DAR2;
        dmatcr = sh4->DMATCR2;
        chcr = sh4->CHCR2;
        dmte = SH4_INTC_DMTE2;
        break;
      case 3:
        sar = sh4->SAR3;
        dar = sh4->DAR3;
        dmatcr = sh4->DMATCR3;
        chcr = sh4->CHCR3;
        dmte = SH4_INTC_DMTE3;
        break;
      default:
        LOG_FATAL("Unexpected DMA channel");
        break;
    }

    uint32_t src = dtr->rw ? dtr->addr : *sar;
    uint32_t dst = dtr->rw ? *dar : dtr->addr;
    int size = *dmatcr * 32;
    as_memcpy(sh4->memory_if->space, dst, src, size);

    // update src / addresses as well as remaining count
    *sar = src + size;
    *dar = dst + size;
    *dmatcr = 0;

    // signal transfer end
    chcr->TE = 1;

    // raise interrupt if requested
    if (chcr->IE) {
      sh4_raise_interrupt(sh4, dmte);
    }
  }
}

struct sh4 *sh4_create(struct dreamcast *dc) {
  struct sh4 *sh4 = dc_create_device(dc, sizeof(struct sh4), "sh", &sh4_init);
  sh4->execute_if = dc_create_execute_interface(&sh4_run);
  sh4->memory_if = dc_create_memory_interface(dc, &sh4_data_map);
  sh4->window_if =
      dc_create_window_interface(NULL, &sh4_paint_debug_menu, NULL);

  g_sh4 = sh4;

  return sh4;
}

void sh4_destroy(struct sh4 *sh4) {
  g_sh4 = NULL;

  if (sh4->code_cache) {
    sh4_cache_destroy(sh4->code_cache);
  }

  dc_destroy_window_interface(sh4->window_if);
  dc_destroy_memory_interface(sh4->memory_if);
  dc_destroy_execute_interface(sh4->execute_if);
  dc_destroy_device((struct device *)sh4);
}

REG_R32(sh4_cb, PDTRA) {
  struct sh4 *sh4 = dc->sh4;
  // magic values to get past 0x8c00b948 in the boot rom:
  // void _8c00b92c(int arg1) {
  //   sysvars->var1 = reg[PDTRA];
  //   for (i = 0; i < 4; i++) {
  //     sysvars->var2 = reg[PDTRA];
  //     if (arg1 == sysvars->var2 & 0x03) {
  //       return;
  //     }
  //   }
  //   reg[PR] = (uint32_t *)0x8c000000;    /* loop forever */
  // }
  // old_PCTRA = reg[PCTRA];
  // i = old_PCTRA | 0x08;
  // reg[PCTRA] = i;
  // reg[PDTRA] = reg[PDTRA] | 0x03;
  // _8c00b92c(3);
  // reg[PCTRA] = i | 0x03;
  // _8c00b92c(3);
  // reg[PDTRA] = reg[PDTRA] & 0xfffe;
  // _8c00b92c(0);
  // reg[PCTRA] = i;
  // _8c00b92c(3);
  // reg[PCTRA] = i | 0x04;
  // _8c00b92c(3);
  // reg[PDTRA] = reg[PDTRA] & 0xfffd;
  // _8c00b92c(0);
  // reg[PCTRA] = old_PCTRA;
  uint32_t pctra = *sh4->PCTRA;
  uint32_t pdtra = *sh4->PDTRA;
  uint32_t v = 0;
  if ((pctra & 0xf) == 0x8 || ((pctra & 0xf) == 0xb && (pdtra & 0xf) != 0x2) ||
      ((pctra & 0xf) == 0xc && (pdtra & 0xf) == 0x2)) {
    v = 3;
  }
  // FIXME cable setting
  // When a VGA cable* is connected
  // 1. The SH4 obtains the cable information from the PIO port.  (PB[9:8] =
  // "00")
  // 2. Set the HOLLY synchronization register for VGA.  (The SYNC output is
  // H-Sync and V-Sync.)
  // 3. When VREG1 = 0 and VREG0 = 0 are written in the AICA register,
  // VIDEO1 = 0 and VIDEO0 = 1 are output.  VIDEO0 is connected to the
  // DVE-DACH pin, and handles switching between RGB and NTSC/PAL.
  //
  // When an RGB(NTSC/PAL) cable* is connected
  // 1. The SH4 obtains the cable information from the PIO port.  (PB[9:8] =
  // "10")
  // 2. Set the HOLLY synchronization register for NTSC/PAL.  (The SYNC
  // output is H-Sync and V-Sync.)
  // 3. When VREG1 = 0 and VREG0 = 0 are written in the AICA register,
  // VIDEO1 = 1 and VIDEO0 = 0 are output.  VIDEO0 is connected to the
  // DVE-DACH pin, and handles switching between RGB and NTSC/PAL.
  //
  // When a stereo A/V cable, an S-jack cable* or an RF converter* is
  // connected
  // 1. The SH4 obtains the cable information from the PIO port.  (PB[9:8] =
  // "11")
  // 2. Set the HOLLY synchronization register for NTSC/PAL.  (The SYNC
  // output is H-Sync and V-Sync.)
  // 3. When VREG1 = 1 and VREG0 = 1 are written in the AICA register,
  // VIDEO1 = 0 and VIDEO0 = 0 are output.  VIDEO0 is connected to the
  // DVE-DACH pin, and handles switching between RGB and NTSC/PAL.
  // v |= 0x3 << 8;
  return v;
}

REG_W32(sh4_cb, MMUCR) {
  struct sh4 *sh4 = dc->sh4;
  if (value) {
    LOG_FATAL("MMU not currently supported");
  }
}

REG_W32(sh4_cb, CCR) {
  struct sh4 *sh4 = dc->sh4;
  sh4->CCR->full = value;
  if (sh4->CCR->ICI) {
    sh4_ccn_reset(sh4);
  }
}

REG_W32(sh4_cb, CHCR0) {
  struct sh4 *sh4 = dc->sh4;
  sh4->CHCR0->full = value;
  sh4_dmac_check(sh4, 0);
}

REG_W32(sh4_cb, CHCR1) {
  struct sh4 *sh4 = dc->sh4;
  sh4->CHCR1->full = value;
  sh4_dmac_check(sh4, 1);
}

REG_W32(sh4_cb, CHCR2) {
  struct sh4 *sh4 = dc->sh4;
  sh4->CHCR2->full = value;
  sh4_dmac_check(sh4, 2);
}

REG_W32(sh4_cb, CHCR3) {
  struct sh4 *sh4 = dc->sh4;
  sh4->CHCR3->full = value;
  sh4_dmac_check(sh4, 3);
}

REG_W32(sh4_cb, DMAOR) {
  struct sh4 *sh4 = dc->sh4;
  sh4->DMAOR->full = value;
  sh4_dmac_check(sh4, 0);
  sh4_dmac_check(sh4, 1);
  sh4_dmac_check(sh4, 2);
  sh4_dmac_check(sh4, 3);
}

REG_W32(sh4_cb, IPRA) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->IPRA = value;
  sh4_intc_reprioritize(sh4);
}

REG_W32(sh4_cb, IPRB) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->IPRB = value;
  sh4_intc_reprioritize(sh4);
}

REG_W32(sh4_cb, IPRC) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->IPRC = value;
  sh4_intc_reprioritize(sh4);
}

REG_W32(sh4_cb, TSTR) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TSTR = value;
  sh4_tmu_update_tstr(sh4);
}

REG_W32(sh4_cb, TCR0) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCR0 = value;
  sh4_tmu_update_tcr(sh4, 0);
}

REG_W32(sh4_cb, TCR1) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCR1 = value;
  sh4_tmu_update_tcr(sh4, 1);
}

REG_W32(sh4_cb, TCR2) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCR2 = value;
  sh4_tmu_update_tcr(sh4, 1);
}

REG_R32(sh4_cb, TCNT0) {
  struct sh4 *sh4 = dc->sh4;
  return sh4_tmu_tcnt(sh4, 0);
}

REG_W32(sh4_cb, TCNT0) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCNT0 = value;
  sh4_tmu_update_tcnt(sh4, 0);
}

REG_R32(sh4_cb, TCNT1) {
  struct sh4 *sh4 = dc->sh4;
  return sh4_tmu_tcnt(sh4, 1);
}

REG_W32(sh4_cb, TCNT1) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCNT1 = value;
  sh4_tmu_update_tcnt(sh4, 1);
}

REG_R32(sh4_cb, TCNT2) {
  struct sh4 *sh4 = dc->sh4;
  return sh4_tmu_tcnt(sh4, 2);
}

REG_W32(sh4_cb, TCNT2) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCNT2 = value;
  sh4_tmu_update_tcnt(sh4, 2);
}

// clang-format off
AM_BEGIN(struct sh4, sh4_data_map)
  AM_RANGE(0x00000000, 0x0021ffff) AM_MOUNT("system rom")
  AM_RANGE(0x0c000000, 0x0cffffff) AM_MOUNT("system ram")

  // main ram mirrors
  AM_RANGE(0x0d000000, 0x0dffffff) AM_MIRROR(0x0c000000)
  AM_RANGE(0x0e000000, 0x0effffff) AM_MIRROR(0x0c000000)
  AM_RANGE(0x0f000000, 0x0fffffff) AM_MIRROR(0x0c000000)

  // external devices
  AM_RANGE(0x005f6000, 0x005f7fff) AM_DEVICE("holly", holly_reg_map)
  AM_RANGE(0x005f8000, 0x005f9fff) AM_DEVICE("pvr", pvr_reg_map)
  AM_RANGE(0x00600000, 0x0067ffff) AM_DEVICE("g2", g2_modem_map)
  AM_RANGE(0x00700000, 0x00710fff) AM_DEVICE("aica", aica_reg_map)
  AM_RANGE(0x00800000, 0x00ffffff) AM_DEVICE("aica", aica_data_map)
  AM_RANGE(0x01000000, 0x01ffffff) AM_DEVICE("g2", g2_expansion0_map)
  AM_RANGE(0x02700000, 0x02ffffff) AM_DEVICE("g2", g2_expansion1_map)
  AM_RANGE(0x04000000, 0x057fffff) AM_DEVICE("pvr", pvr_vram_map)
  AM_RANGE(0x10000000, 0x11ffffff) AM_DEVICE("ta", ta_fifo_map)
  AM_RANGE(0x14000000, 0x17ffffff) AM_DEVICE("g2", g2_expansion2_map)

  // internal registers
  AM_RANGE(0x1e000000, 0x1fffffff) AM_HANDLE("sh4 reg",
                                             (r8_cb)&sh4_reg_r8,
                                             (r16_cb)&sh4_reg_r16,
                                             (r32_cb)&sh4_reg_r32,
                                             NULL,
                                             (w8_cb)&sh4_reg_w8,
                                             (w16_cb)&sh4_reg_w16,
                                             (w32_cb)&sh4_reg_w32,
                                             NULL)

  // physical mirrors
  AM_RANGE(0x20000000, 0x3fffffff) AM_MIRROR(0x00000000)  // p0
  AM_RANGE(0x40000000, 0x5fffffff) AM_MIRROR(0x00000000)  // p0
  AM_RANGE(0x60000000, 0x7fffffff) AM_MIRROR(0x00000000)  // p0
  AM_RANGE(0x80000000, 0x9fffffff) AM_MIRROR(0x00000000)  // p1
  AM_RANGE(0xa0000000, 0xbfffffff) AM_MIRROR(0x00000000)  // p2
  AM_RANGE(0xc0000000, 0xdfffffff) AM_MIRROR(0x00000000)  // p3
  AM_RANGE(0xe0000000, 0xffffffff) AM_MIRROR(0x00000000)  // p4

  // internal cache and sq only accessible through p4
  AM_RANGE(0x7c000000, 0x7fffffff) AM_HANDLE("sh4 cache",
                                             (r8_cb)&sh4_cache_r8,
                                             (r16_cb)&sh4_cache_r16,
                                             (r32_cb)&sh4_cache_r32,
                                             (r64_cb)&sh4_cache_r64,
                                             (w8_cb)&sh4_cache_w8,
                                             (w16_cb)&sh4_cache_w16,
                                             (w32_cb)&sh4_cache_w32,
                                             (w64_cb)&sh4_cache_w64)

  AM_RANGE(0xe0000000, 0xe3ffffff) AM_HANDLE("sh4 sq",
                                             (r8_cb)&sh4_sq_r8,
                                             (r16_cb)&sh4_sq_r16,
                                             (r32_cb)&sh4_sq_r32,
                                             NULL,
                                             (w8_cb)&sh4_sq_w8,
                                             (w16_cb)&sh4_sq_w16,
                                             (w32_cb)&sh4_sq_w32,
                                             NULL)
AM_END();
// clang-format on
