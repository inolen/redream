#include "hw/sh4/sh4.h"
#include "core/math.h"
#include "core/string.h"
#include "hw/aica/aica.h"
#include "hw/debugger.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/memory.h"
#include "hw/pvr/pvr.h"
#include "hw/pvr/ta.h"
#include "hw/rom/boot.h"
#include "hw/rom/flash.h"
#include "hw/scheduler.h"
#include "hw/sh4/x64/sh4_dispatch.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_analyze.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/frontend/sh4/sh4_translate.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "sys/time.h"
#include "ui/nuklear.h"

DEFINE_AGGREGATE_COUNTER(sh4_instrs);
DEFINE_AGGREGATE_COUNTER(sh4_sr_updates);

/* callbacks to service sh4_reg_read / sh4_reg_write calls */
struct reg_cb sh4_cb[NUM_SH4_REGS];

static void sh4_swap_gpr_bank(struct sh4 *sh4) {
  for (int s = 0; s < 8; s++) {
    uint32_t tmp = sh4->ctx.r[s];
    sh4->ctx.r[s] = sh4->ctx.ralt[s];
    sh4->ctx.ralt[s] = tmp;
  }
}

static void sh4_swap_fpr_bank(struct sh4 *sh4) {
  for (int s = 0; s <= 15; s++) {
    uint32_t tmp = sh4->ctx.fr[s];
    sh4->ctx.fr[s] = sh4->ctx.xf[s];
    sh4->ctx.xf[s] = tmp;
  }
}

static void sh4_invalid_instr(void *data, uint32_t addr) {
  /*struct sh4 *self = reinterpret_cast<SH4 *>(ctx->sh4);

  auto it = self->breakpoints.find(addr);
  CHECK_NE(it, self->breakpoints.end());

  // force the main loop to break
  self->ctx.num_cycles = 0;

  // let the debugger know execution has stopped
  self->dc->debugger->Trap();*/
}

void sh4_sr_updated(void *data, uint32_t old_sr) {
  struct sh4 *sh4 = data;
  struct sh4_ctx *ctx = &sh4->ctx;

  prof_counter_add(COUNTER_sh4_sr_updates, 1);

  if ((ctx->sr & RB) != (old_sr & RB)) {
    sh4_swap_gpr_bank(sh4);
  }

  if ((ctx->sr & I) != (old_sr & I) || (ctx->sr & BL) != (old_sr & BL)) {
    sh4_intc_update_pending(sh4);
  }
}

void sh4_fpscr_updated(void *data, uint32_t old_fpscr) {
  struct sh4 *sh4 = data;
  struct sh4_ctx *ctx = &sh4->ctx;

  if ((ctx->fpscr & FR) != (old_fpscr & FR)) {
    sh4_swap_fpr_bank(sh4);
  }
}

static uint32_t sh4_reg_read(struct sh4 *sh4, uint32_t addr,
                             uint32_t data_mask) {
  uint32_t offset = SH4_REG_OFFSET(addr);
  reg_read_cb read = sh4_cb[offset].read;
  if (read) {
    return read(sh4->dc);
  }
  return sh4->reg[offset];
}

static void sh4_reg_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                          uint32_t data_mask) {
  uint32_t offset = SH4_REG_OFFSET(addr);
  reg_write_cb write = sh4_cb[offset].write;
  if (write) {
    write(sh4->dc, data);
    return;
  }
  sh4->reg[offset] = data;
}

static void sh4_translate(void *data, uint32_t addr, struct ir *ir, int fastmem,
                          int *size) {
  struct sh4 *sh4 = data;

  /* analyze the guest block to get its size, cycle count, etc. */
  struct sh4_analysis as = {0};
  as.addr = addr;
  if (fastmem) {
    as.flags |= SH4_FASTMEM;
  }
  if (sh4->ctx.fpscr & PR) {
    as.flags |= SH4_DOUBLE_PR;
  }
  if (sh4->ctx.fpscr & SZ) {
    as.flags |= SH4_DOUBLE_SZ;
  }
  sh4_analyze_block(sh4->jit, &as);

  /* yield control once remaining cycles are executed */
  struct ir_value *remaining_cycles = ir_load_context(
      ir, offsetof(struct sh4_ctx, remaining_cycles), VALUE_I32);
  struct ir_value *done = ir_cmp_sle(ir, remaining_cycles, ir_alloc_i32(ir, 0));
  ir_branch_true(ir, done, ir_alloc_i64(ir, (uint64_t)sh4_dispatch_leave));

  /* handle pending interrupts */
  struct ir_value *pending_intr = ir_load_context(
      ir, offsetof(struct sh4_ctx, pending_interrupts), VALUE_I64);
  ir_branch_true(ir, pending_intr,
                 ir_alloc_i64(ir, (uint64_t)sh4_dispatch_interrupt));

  /* update remaining cycles */
  remaining_cycles = ir_sub(ir, remaining_cycles, ir_alloc_i32(ir, as.cycles));
  ir_store_context(ir, offsetof(struct sh4_ctx, remaining_cycles),
                   remaining_cycles);

  /* update instruction run count */
  struct ir_value *ran_instrs =
      ir_load_context(ir, offsetof(struct sh4_ctx, ran_instrs), VALUE_I64);
  ran_instrs = ir_add(ir, ran_instrs, ir_alloc_i64(ir, as.size / 2));
  ir_store_context(ir, offsetof(struct sh4_ctx, ran_instrs), ran_instrs);

  /* translate the actual block */
  for (int i = 0; i < as.size;) {
    struct sh4_instr instr = {0};
    struct sh4_instr delay_instr = {0};

    instr.addr = addr + i;
    instr.opcode = as_read16(sh4->memory_if->space, instr.addr);
    sh4_disasm(&instr);

    i += 2;

    if (instr.flags & SH4_FLAG_DELAYED) {
      delay_instr.addr = addr + i;
      delay_instr.opcode = as_read16(sh4->memory_if->space, delay_instr.addr);

      /* instruction must be valid, breakpoints on delay instructions aren't
         currently supported */
      CHECK(sh4_disasm(&delay_instr));

      /* delay instruction itself should never have a delay instr */
      CHECK(!(delay_instr.flags & SH4_FLAG_DELAYED));

      i += 2;
    }

    sh4_emit_instr((struct sh4_frontend *)sh4->frontend, ir, as.flags, &instr,
                   &delay_instr);
  }

  /* if the block terminates before a branch, fallthrough to the next pc */
  struct ir_instr *tail_instr =
      list_last_entry(&ir->instrs, struct ir_instr, it);

  if (tail_instr->op != OP_STORE_CONTEXT ||
      tail_instr->arg[0]->i32 != offsetof(struct sh4_ctx, pc)) {
    ir_store_context(ir, offsetof(struct sh4_ctx, pc),
                     ir_alloc_i32(ir, addr + as.size));
  }

  /* the default emitters won't actually insert calls / branches to the
     appropriate dispatch routines (as how each is invoked is specific
     to the particular dispatch backend) */
  list_for_each_entry_safe_reverse(instr, &ir->instrs, struct ir_instr, it) {
    if (instr->op != OP_STORE_CONTEXT ||
        instr->arg[0]->i32 != offsetof(struct sh4_ctx, pc)) {
      continue;
    }

    int direct = ir_is_constant(instr->arg[1]);

    /* insert dispatch call immediately after pc store */
    ir->current_instr = instr;

    if (direct) {
      ir_call(ir, ir_alloc_i64(ir, (uint64_t)sh4_dispatch_static));
    } else {
      ir_branch(ir, ir_alloc_i64(ir, (uint64_t)sh4_dispatch_dynamic));
    }
  }

  /* return size */
  *size = as.size;
}

void sh4_clear_interrupt(struct sh4 *sh4, enum sh4_interrupt intr) {
  sh4->requested_interrupts &= ~sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

void sh4_raise_interrupt(struct sh4 *sh4, enum sh4_interrupt intr) {
  sh4->requested_interrupts |= sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

static void sh4_debug_menu(struct device *dev, struct nk_context *ctx) {
  struct sh4 *sh4 = (struct sh4 *)dev;

  nk_layout_row_push(ctx, 30.0f);

  if (nk_menu_begin_label(ctx, "SH4", NK_TEXT_LEFT, nk_vec2(200.0f, 200.0f))) {
    nk_layout_row_dynamic(ctx, DEBUG_MENU_HEIGHT, 1);

    if (nk_button_label(ctx, "clear cache")) {
      jit_invalidate_blocks(sh4->jit);
    }

    int dumping = jit_is_dumping(sh4->jit);
    if (!dumping && nk_button_label(ctx, "start dumping blocks")) {
      jit_toggle_dumping(sh4->jit);
    } else if (dumping && nk_button_label(ctx, "stop dumping blocks")) {
      jit_toggle_dumping(sh4->jit);
    }

    nk_menu_end(ctx);
  }
}

void sh4_reset(struct sh4 *sh4, uint32_t pc) {
  jit_free_blocks(sh4->jit);

  /* reset context */
  sh4->ctx.pc = pc;
  sh4->ctx.r[15] = 0x8d000000;
  sh4->ctx.pr = 0x0;
  sh4->ctx.sr = 0x700000f0;
  sh4->ctx.fpscr = 0x00040001;

/* initialize registers */
#define SH4_REG(addr, name, default, type) \
  sh4->reg[name] = default;                \
  sh4->name = (type *)&sh4->reg[name];
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

  /* reset interrupts */
  sh4_intc_reprioritize(sh4);

  sh4->execute_if->running = 1;
}

static void sh4_run(struct device *dev, int64_t ns) {
  PROF_ENTER("cpu", "sh4_run");

  struct sh4 *sh4 = (struct sh4 *)dev;

  int64_t cycles = NANO_TO_CYCLES(ns, SH4_CLOCK_FREQ);
  sh4->ctx.remaining_cycles = (int)cycles;
  sh4->ctx.ran_instrs = 0;
  sh4_dispatch_enter();
  prof_counter_add(COUNTER_sh4_instrs, sh4->ctx.ran_instrs);

  PROF_LEAVE();
}

static bool sh4_init(struct device *dev) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct dreamcast *dc = sh4->dc;

  /* initialize jit and its interfaces */
  sh4->jit = jit_create("sh4");

  sh4_dispatch_init(sh4, sh4->jit, &sh4->ctx, sh4->memory_if->space->base);

  struct jit_guest *guest = &sh4->guest;
  guest->ctx = &sh4->ctx;
  guest->mem = sh4->memory_if->space->base;
  guest->space = sh4->memory_if->space;
  guest->lookup_code = &sh4_dispatch_lookup_code;
  guest->cache_code = &sh4_dispatch_cache_code;
  guest->invalidate_code = &sh4_dispatch_invalidate_code;
  guest->patch_edge = &sh4_dispatch_patch_edge;
  guest->restore_edge = &sh4_dispatch_restore_edge;
  guest->r8 = &as_read8;
  guest->r16 = &as_read16;
  guest->r32 = &as_read32;
  guest->w8 = &as_write8;
  guest->w16 = &as_write16;
  guest->w32 = &as_write32;

  struct sh4_frontend *frontend =
      (struct sh4_frontend *)sh4_frontend_create(sh4->jit);
  frontend->data = sh4;
  frontend->translate = &sh4_translate;
  frontend->invalid_instr = &sh4_invalid_instr;
  frontend->sq_prefetch = &sh4_ccn_sq_prefetch;
  frontend->sr_updated = &sh4_sr_updated;
  frontend->fpscr_updated = &sh4_fpscr_updated;
  sh4->frontend = (struct jit_frontend *)frontend;

  struct jit_backend *backend =
      x64_backend_create(sh4->jit, sh4_code, sh4_code_size, sh4_stack_size);
  sh4->backend = backend;

  if (!jit_init(sh4->jit, &sh4->guest, sh4->frontend, sh4->backend)) {
    return false;
  }

  return true;
}

void sh4_destroy(struct sh4 *sh4) {
  if (sh4->jit) {
    jit_destroy(sh4->jit);
  }

  if (sh4->backend) {
    x64_backend_destroy(sh4->backend);
  }

  if (sh4->frontend) {
    sh4_frontend_destroy(sh4->frontend);
  }

  dc_destroy_window_interface(sh4->window_if);
  dc_destroy_memory_interface(sh4->memory_if);
  dc_destroy_execute_interface(sh4->execute_if);
  dc_destroy_device((struct device *)sh4);
}

struct sh4 *sh4_create(struct dreamcast *dc) {
  struct sh4 *sh4 = dc_create_device(dc, sizeof(struct sh4), "sh", &sh4_init);
  sh4->execute_if = dc_create_execute_interface(&sh4_run, 0);
  sh4->memory_if = dc_create_memory_interface(dc, &sh4_data_map);
  sh4->window_if =
      dc_create_window_interface(&sh4_debug_menu, NULL, NULL, NULL);

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

/* clang-format off */
AM_BEGIN(struct sh4, sh4_data_map)
  AM_RANGE(0x00000000, 0x001fffff) AM_DEVICE("boot", boot_rom_map)
  AM_RANGE(0x00200000, 0x0021ffff) AM_DEVICE("flash", flash_rom_map)
  AM_RANGE(0x0c000000, 0x0cffffff) AM_MOUNT("system ram")

  /* main ram mirrors */
  AM_RANGE(0x0d000000, 0x0dffffff) AM_MIRROR(0x0c000000)
  AM_RANGE(0x0e000000, 0x0effffff) AM_MIRROR(0x0c000000)
  AM_RANGE(0x0f000000, 0x0fffffff) AM_MIRROR(0x0c000000)

  /* external devices */
  AM_RANGE(0x005f0000, 0x005f7fff) AM_DEVICE("holly", holly_reg_map)
  AM_RANGE(0x005f8000, 0x005f9fff) AM_DEVICE("pvr", pvr_reg_map)
  AM_RANGE(0x00600000, 0x0067ffff) AM_DEVICE("holly", holly_modem_map)
  AM_RANGE(0x00700000, 0x00710fff) AM_DEVICE("aica", aica_reg_map)
  AM_RANGE(0x00800000, 0x00ffffff) AM_DEVICE("aica", aica_data_map)
  AM_RANGE(0x01000000, 0x01ffffff) AM_DEVICE("holly", holly_expansion0_map)
  AM_RANGE(0x02700000, 0x02ffffff) AM_DEVICE("holly", holly_expansion1_map)
  AM_RANGE(0x04000000, 0x057fffff) AM_DEVICE("pvr", pvr_vram_map)
  AM_RANGE(0x10000000, 0x11ffffff) AM_DEVICE("ta", ta_fifo_map)
  AM_RANGE(0x14000000, 0x17ffffff) AM_DEVICE("holly", holly_expansion2_map)

  /* internal registers */
  AM_RANGE(0x1c000000, 0x1fffffff) AM_HANDLE("sh4 reg",
                                             (mmio_read_cb)&sh4_reg_read,
                                             (mmio_write_cb)&sh4_reg_write,
                                             NULL, NULL)

  /* physical mirrors */
  AM_RANGE(0x20000000, 0x3fffffff) AM_MIRROR(0x00000000)  /* p0 */
  AM_RANGE(0x40000000, 0x5fffffff) AM_MIRROR(0x00000000)  /* p0 */
  AM_RANGE(0x60000000, 0x7fffffff) AM_MIRROR(0x00000000)  /* p0 */
  AM_RANGE(0x80000000, 0x9fffffff) AM_MIRROR(0x00000000)  /* p1 */
  AM_RANGE(0xa0000000, 0xbfffffff) AM_MIRROR(0x00000000)  /* p2 */
  AM_RANGE(0xc0000000, 0xdfffffff) AM_MIRROR(0x00000000)  /* p3 */
  AM_RANGE(0xe0000000, 0xffffffff) AM_MIRROR(0x00000000)  /* p4 */

  /* internal cache and sq only accessible through p4 */
  AM_RANGE(0x7c000000, 0x7fffffff) AM_HANDLE("sh4 cache",
                                             (mmio_read_cb)&sh4_ccn_cache_read,
                                             (mmio_write_cb)&sh4_ccn_cache_write,
                                             NULL, NULL)
  AM_RANGE(0xe0000000, 0xe3ffffff) AM_HANDLE("sh4 sq",
                                             (mmio_read_cb)&sh4_ccn_sq_read,
                                             (mmio_write_cb)&sh4_ccn_sq_write,
                                             NULL, NULL)
AM_END();
/* clang-format on */
