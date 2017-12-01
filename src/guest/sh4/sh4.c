#include "guest/sh4/sh4.h"
#include "core/core.h"
#include "guest/bios/bios.h"
#include "guest/dreamcast.h"
#include "guest/memory.h"
#include "guest/scheduler.h"
#include "imgui.h"
#include "jit/frontend/sh4/sh4_fallback.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/frontend/sh4/sh4_guest.h"
#include "jit/jit.h"
#include "stats.h"

#if ARCH_X64
#include "jit/backend/x64/x64_backend.h"
#else
#include "jit/backend/interp/interp_backend.h"
#endif

/* callbacks to service sh4_reg_read / sh4_reg_write calls */
struct reg_cb sh4_cb[SH4_NUM_REGS];

struct sh4_exception_info sh4_exceptions[SH4_NUM_EXCEPTIONS] = {
#define SH4_EXC(name, expevt, offset, prilvl, priord) \
  {expevt, offset, prilvl, priord},
#include "guest/sh4/sh4_exc.inc"
#undef SH4_EXC
};

struct sh4_interrupt_info sh4_interrupts[SH4_NUM_INTERRUPTS] = {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) \
  {intevt, pri, ipr, ipr_shift},
#include "guest/sh4/sh4_int.inc"
#undef SH4_INT
};

static void sh4_sr_updated(struct sh4 *sh4, uint32_t old_sr) {
  struct sh4_context *ctx = &sh4->ctx;

  if ((ctx->sr & RB_MASK) != (old_sr & RB_MASK)) {
    sh4_swap_gpr_bank(ctx);
  }

  if ((ctx->sr & I_MASK) != (old_sr & I_MASK) ||
      (ctx->sr & BL_MASK) != (old_sr & BL_MASK)) {
    sh4_intc_update_pending(sh4);
  }
}

static void sh4_fpscr_updated(struct sh4 *sh4, uint32_t old_fpscr) {
  struct sh4_context *ctx = &sh4->ctx;

  if (!(old_fpscr & ENABLE_MASK) && (ctx->fpscr & ENABLE_MASK)) {
    LOG_WARNING("sh4_fpscr_updated fpu exceptions aren't supported");
  }

  if ((ctx->fpscr & FR_MASK) != (old_fpscr & FR_MASK)) {
    sh4_swap_fpr_bank(ctx);
  }
}

static void sh4_sleep(void *data) {
  struct sh4 *sh4 = data;

  /* standby / deep sleep mode are not currently supported */
  CHECK_EQ(sh4->STBCR->STBY, 0);
  CHECK_EQ(sh4->STBCR2->DSLP, 0);

  /* do nothing but spin on the current pc until an interrupt is raised */
  sh4->ctx.sleep_mode = 1;
}

static void sh4_exception(struct sh4 *sh4, enum sh4_exception exc) {
  struct sh4_exception_info *exc_info = &sh4_exceptions[exc];

  /* let the custom exception handler have a first chance */
  if (sh4->exc_handler && sh4->exc_handler(sh4->exc_handler_data, exc)) {
    return;
  }

  /* ensure sr is up to date */
  sh4_implode_sr(&sh4->ctx);

  *sh4->EXPEVT = exc_info->expevt;
  sh4->ctx.spc = sh4->ctx.pc;
  sh4->ctx.ssr = sh4->ctx.sr;
  sh4->ctx.sgr = sh4->ctx.r[15];
  sh4->ctx.sr |= (BL_MASK | MD_MASK | RB_MASK);
  sh4->ctx.pc = sh4->ctx.vbr + exc_info->offset;
  sh4_sr_updated(sh4, sh4->ctx.ssr);
}

static void sh4_check_interrupts(struct sh4 *sh4) {
  if (!sh4->ctx.pending_interrupts) {
    return;
  }

  /* process the highest priority in the pending vector */
  int n = 63 - clz64(sh4->ctx.pending_interrupts);
  enum sh4_interrupt intr = sh4->sorted_interrupts[n];
  struct sh4_interrupt_info *int_info = &sh4_interrupts[intr];

  /* ensure sr is up to date */
  sh4_implode_sr(&sh4->ctx);

  *sh4->INTEVT = int_info->intevt;
  sh4->ctx.spc = sh4->ctx.pc;
  sh4->ctx.ssr = sh4->ctx.sr;
  sh4->ctx.sgr = sh4->ctx.r[15];
  sh4->ctx.sr |= (BL_MASK | MD_MASK | RB_MASK);
  sh4->ctx.pc = sh4->ctx.vbr + 0x600;
  sh4->ctx.sleep_mode = 0;
  sh4_sr_updated(sh4, sh4->ctx.ssr);
}

static void sh4_link_code(struct sh4 *sh4, void *branch, uint32_t target) {
  jit_link_code(sh4->jit, branch, target);
}

static void sh4_compile_code(struct sh4 *sh4, uint32_t addr) {
  jit_compile_code(sh4->jit, addr);
}

static void sh4_invalid_instr(struct sh4 *sh4) {
  struct memory *mem = sh4->dc->mem;
  struct bios *bios = sh4->dc->bios;

  /* TODO write tests to confirm if any other instructions generate illegal
     instruction exceptions */
  const uint16_t SH4_INVALID_INSTR = 0xfffd;

  /* let internal systems have a first chance at illegal instructions. note,
     they will write out invalid instructions other than SH4_INVALID_INSTR
     in order to trap */
  if (bios_invalid_instr(bios)) {
    return;
  }

  if (sh4_dbg_invalid_instr(sh4)) {
    return;
  }

  uint32_t pc = sh4->ctx.pc;
  uint16_t data = sh4_read16(mem, pc);
  struct jit_opdef *def = sh4_get_opdef(data);
  enum sh4_exception exc = SH4_EXC_ILLINSTR;

  /* op may be valid if the delay slot raised this */
  if (def->op != SH4_OP_INVALID) {
    data = sh4_read16(mem, pc + 2);
    def = sh4_get_opdef(data);
    exc = SH4_EXC_ILLSLOT;
  }

  if (data != SH4_INVALID_INSTR) {
    return;
  }

  CHECK_EQ(def->op, SH4_OP_INVALID);

  sh4_exception(sh4, exc);
}

static void sh4_run(struct device *dev, int64_t ns) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct sh4_context *ctx = &sh4->ctx;
  struct jit *jit = sh4->jit;

  int cycles = (int)NANO_TO_CYCLES(ns, SH4_CLOCK_FREQ);
  cycles = MAX(cycles, 1);

  jit_run(sh4->jit, cycles);

  prof_counter_add(COUNTER_sh4_instrs, sh4->ctx.ran_instrs);
}

static void sh4_guest_destroy(struct jit_guest *guest) {
  free((struct sh4_guest *)guest);
}

static struct jit_guest *sh4_guest_create(struct sh4 *sh4) {
  struct sh4_guest *guest = calloc(1, sizeof(struct sh4_guest));

  /* dispatch cache */
  guest->addr_mask = 0x00fffffe;

  /* memory interface */
  guest->ctx = &sh4->ctx;
  guest->membase = sh4_base(sh4->dc->mem);
  guest->mem = sh4->dc->mem;
  guest->lookup = &sh4_lookup;
  guest->r8 = &sh4_read8;
  guest->r16 = &sh4_read16;
  guest->r32 = &sh4_read32;
  guest->w8 = &sh4_write8;
  guest->w16 = &sh4_write16;
  guest->w32 = &sh4_write32;

  /* runtime interface */
  guest->data = sh4;
  guest->offset_pc = (int)offsetof(struct sh4_context, pc);
  guest->offset_cycles = (int)offsetof(struct sh4_context, run_cycles);
  guest->offset_instrs = (int)offsetof(struct sh4_context, ran_instrs);
  guest->offset_interrupts =
      (int)offsetof(struct sh4_context, pending_interrupts);
  guest->compile_code = (jit_compile_cb)&sh4_compile_code;
  guest->link_code = (jit_link_cb)&sh4_link_code;
  guest->check_interrupts = (jit_interrupt_cb)&sh4_check_interrupts;
  guest->invalid_instr = (sh4_invalid_instr_cb)&sh4_invalid_instr;
  guest->ltlb = (sh4_ltlb_cb)&sh4_mmu_ltlb;
  guest->pref = (sh4_pref_cb)&sh4_ccn_pref;
  guest->sleep = (sh4_sleep_cb)&sh4_sleep;
  guest->sr_updated = (sh4_sr_updated_cb)&sh4_sr_updated;
  guest->fpscr_updated = (sh4_fpscr_updated_cb)&sh4_fpscr_updated;

  return (struct jit_guest *)guest;
}

static int sh4_init(struct device *dev) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct dreamcast *dc = sh4->dc;

  /* initialize jit */
  sh4->guest = sh4_guest_create(sh4);
  sh4->frontend = sh4_frontend_create(sh4->guest);
#if ARCH_X64
  DEFINE_JIT_CODE_BUFFER(sh4_code);
  sh4->backend = x64_backend_create(sh4->guest, sh4_code, sizeof(sh4_code));
#else
  sh4->backend = interp_backend_create(sh4->guest, sh4->frontend);
#endif
  sh4->jit = jit_create("sh4", sh4->frontend, sh4->backend);

  return 1;
}

void sh4_clear_interrupt(struct sh4 *sh4, enum sh4_interrupt intr) {
  sh4->requested_interrupts &= ~sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

void sh4_raise_interrupt(struct sh4 *sh4, enum sh4_interrupt intr) {
  sh4->requested_interrupts |= sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

void sh4_set_exception_handler(struct sh4 *sh4,
                               sh4_exception_handler_cb handler, void *data) {
  sh4->exc_handler = handler;
  sh4->exc_handler_data = data;
}

void sh4_reset(struct sh4 *sh4, uint32_t pc) {
  jit_free_code(sh4->jit);

  /* reset context */
  memset(&sh4->ctx, 0, sizeof(sh4->ctx));
  sh4->ctx.pc = pc;
  sh4->ctx.r[15] = 0x8d000000;
  sh4->ctx.pr = 0x0;
  sh4->ctx.sr = 0x700000f0;
  sh4->ctx.fpscr = 0x00040001;
  sh4_explode_sr(&sh4->ctx);

/* initialize registers */
#define SH4_REG(addr, name, default, type) \
  sh4->reg[name] = default;                \
  sh4->name = (type *)&sh4->reg[name];
#include "guest/sh4/sh4_regs.inc"
#undef SH4_REG

  /* reset tlb */
  memset(sh4->utlb_sq_map, 0, sizeof(sh4->utlb_sq_map));
  memset(sh4->utlb, 0, sizeof(sh4->utlb));

  /* reset interrupts */
  sh4_intc_reprioritize(sh4);

  sh4->runif.running = 1;
}

#ifdef HAVE_IMGUI
void sh4_debug_menu(struct sh4 *sh4) {
  struct jit *jit = sh4->jit;

  if (igBeginMainMenuBar()) {
    if (igBeginMenu("SH4", 1)) {
      if (igMenuItem("clear cache", NULL, 0, 1)) {
        jit_invalidate_code(jit);
      }

      if (!jit->dump_code) {
        if (igMenuItem("start dumping code", NULL, 0, 1)) {
          jit->dump_code = 1;
          jit_invalidate_code(jit);
        }
      } else {
        if (igMenuItem("stop dumping code", NULL, 1, 1)) {
          jit->dump_code = 0;
        }
      }

      if (igMenuItem("log reg access", NULL, sh4->log_regs, 1)) {
        sh4->log_regs = !sh4->log_regs;
      }

      if (igMenuItem("tmu stats", NULL, sh4->tmu_stats, 1)) {
        sh4->tmu_stats = !sh4->tmu_stats;
      }

      igEndMenu();
    }

    igEndMainMenuBar();
  }

  if (sh4->tmu_stats) {
    sh4_tmu_debug_menu(sh4);
  }
}
#endif

void sh4_destroy(struct sh4 *sh4) {
  jit_destroy(sh4->jit);
  sh4_guest_destroy(sh4->guest);
  sh4->frontend->destroy(sh4->frontend);
  sh4->backend->destroy(sh4->backend);
  dc_destroy_device((struct device *)sh4);
}

struct sh4 *sh4_create(struct dreamcast *dc) {
  struct sh4 *sh4 =
      dc_create_device(dc, sizeof(struct sh4), "sh", &sh4_init, NULL);

  /* setup debug interface */
  sh4->dbgif.enabled = 1;
  sh4->dbgif.num_regs = &sh4_dbg_num_registers;
  sh4->dbgif.step = &sh4_dbg_step;
  sh4->dbgif.add_bp = &sh4_dbg_add_breakpoint;
  sh4->dbgif.rem_bp = &sh4_dbg_remove_breakpoint;
  sh4->dbgif.read_mem = &sh4_dbg_read_memory;
  sh4->dbgif.read_reg = &sh4_dbg_read_register;

  /* setup run interface */
  sh4->runif.enabled = 1;
  sh4->runif.run = &sh4_run;

  return sh4;
}

REG_R32(sh4_cb, PDTRA) {
  struct sh4 *sh4 = dc->sh4;
  /*
   * magic values to get past 0x8c00b948 in the boot rom:
   * void _8c00b92c(int arg1) {
   *   sysvars->var1 = reg[PDTRA];
   *   for (i = 0; i < 4; i++) {
   *     sysvars->var2 = reg[PDTRA];
   *     if (arg1 == sysvars->var2 & 0x03) {
   *       return;
   *     }
   *   }
   *   reg[PR] = (uint32_t *)0x8c000000;
   * }
   * old_PCTRA = reg[PCTRA];
   * i = old_PCTRA | 0x08;
   * reg[PCTRA] = i;
   * reg[PDTRA] = reg[PDTRA] | 0x03;
   * _8c00b92c(3);
   * reg[PCTRA] = i | 0x03;
   * _8c00b92c(3);
   * reg[PDTRA] = reg[PDTRA] & 0xfffe;
   * _8c00b92c(0);
   * reg[PCTRA] = i;
   * _8c00b92c(3);
   * reg[PCTRA] = i | 0x04;
   * _8c00b92c(3);
   * reg[PDTRA] = reg[PDTRA] & 0xfffd;
   * _8c00b92c(0);
   * reg[PCTRA] = old_PCTRA;
   */
  uint32_t pctra = *sh4->PCTRA;
  uint32_t pdtra = *sh4->PDTRA;
  uint32_t v = 0;
  if ((pctra & 0xf) == 0x8 || ((pctra & 0xf) == 0xb && (pdtra & 0xf) != 0x2) ||
      ((pctra & 0xf) == 0xc && (pdtra & 0xf) == 0x2)) {
    v = 3;
  }

  /* FIXME cable setting */
  int cable_type = 3;
  v |= (cable_type << 8);
  return v;
}
