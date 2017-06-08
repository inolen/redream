#include "hw/sh4/sh4.h"
#include "core/math.h"
#include "core/string.h"
#include "hw/aica/aica.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/memory.h"
#include "hw/pvr/pvr.h"
#include "hw/pvr/ta.h"
#include "hw/rom/bios.h"
#include "hw/rom/flash.h"
#include "hw/scheduler.h"
#include "jit/frontend/sh4/sh4_fallback.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/frontend/sh4/sh4_guest.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "render/imgui.h"
#include "sys/time.h"

#if ARCH_X64
#include "jit/backend/x64/x64_backend.h"
#else
#include "jit/backend/interp/interp_backend.h"
#endif

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

void sh4_sr_updated(void *data, uint32_t old_sr) {
  struct sh4 *sh4 = data;
  struct sh4_context *ctx = &sh4->ctx;

  prof_counter_add(COUNTER_sh4_sr_updates, 1);

  if ((ctx->sr & RB_MASK) != (old_sr & RB_MASK)) {
    sh4_swap_gpr_bank(sh4);
  }

  if ((ctx->sr & I_MASK) != (old_sr & I_MASK) ||
      (ctx->sr & BL_MASK) != (old_sr & BL_MASK)) {
    sh4_intc_update_pending(sh4);
  }
}

void sh4_fpscr_updated(void *data, uint32_t old_fpscr) {
  struct sh4 *sh4 = data;
  struct sh4_context *ctx = &sh4->ctx;

  if ((ctx->fpscr & FR_MASK) != (old_fpscr & FR_MASK)) {
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

static void sh4_invalid_instr(void *data) {
  struct sh4 *sh4 = data;

  if (sh4_dbg_invalid_instr(sh4)) {
    return;
  }

  LOG_FATAL("Unhandled invalid instruction at 0x%08x", sh4->ctx.pc);
}

void sh4_clear_interrupt(struct sh4 *sh4, enum sh4_interrupt intr) {
  sh4->requested_interrupts &= ~sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

void sh4_raise_interrupt(struct sh4 *sh4, enum sh4_interrupt intr) {
  sh4->requested_interrupts |= sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

void sh4_reset(struct sh4 *sh4, uint32_t pc) {
  jit_free_blocks(sh4->jit);

  /* reset context */
  memset(&sh4->ctx, 0, sizeof(sh4->ctx));
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
  struct sh4_context *ctx = &sh4->ctx;
  struct jit *jit = sh4->jit;

  int cycles = (int)NANO_TO_CYCLES(ns, SH4_CLOCK_FREQ);
  cycles = MAX(cycles, 1);

  jit_run(sh4->jit, cycles);

  prof_counter_add(COUNTER_sh4_instrs, sh4->ctx.ran_instrs);

  PROF_LEAVE();
}

static void sh4_debug_menu(struct device *dev) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct jit *jit = sh4->jit;

  if (igBeginMainMenuBar()) {
    if (igBeginMenu("SH4", 1)) {
      if (igMenuItem("clear cache", NULL, 0, 1)) {
        jit_invalidate_blocks(sh4->jit);
      }

      if (!jit->dump_blocks) {
        if (igMenuItem("start dumping blocks", NULL, 0, 1)) {
          jit->dump_blocks = 1;
          jit_invalidate_blocks(jit);
        }
      } else {
        if (igMenuItem("stop dumping blocks", NULL, 1, 1)) {
          jit->dump_blocks = 0;
        }
      }

      igEndMenu();
    }

    igEndMainMenuBar();
  }
}

static int sh4_init(struct device *dev) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct dreamcast *dc = sh4->dc;

  /* place code buffer in data segment (as opposed to allocating on the heap) to
     keep it within 2 GB of the code segment, enabling the x64 backend to use
     RIP-relative offsets when calling functions */
  static uint8_t sh4_code[0x800000];

  sh4->frontend = sh4_frontend_create();

#if ARCH_X64
  sh4->backend = x64_backend_create(sh4_code, sizeof(sh4_code));
#else
  sh4->backend = interp_backend_create();
#endif

  {
    sh4->guest = sh4_guest_create();

    sh4->guest->addr_mask = 0x00fffffe;

    sh4->guest->data = sh4;
    sh4->guest->offset_pc = (int)offsetof(struct sh4_context, pc);
    sh4->guest->offset_cycles = (int)offsetof(struct sh4_context, run_cycles);
    sh4->guest->offset_instrs = (int)offsetof(struct sh4_context, ran_instrs);
    sh4->guest->offset_interrupts =
        (int)offsetof(struct sh4_context, pending_interrupts);
    sh4->guest->interrupt_check = &sh4_intc_check_pending;

    sh4->guest->ctx = &sh4->ctx;
    sh4->guest->mem = as_translate(sh4->memory_if->space, 0x0);
    sh4->guest->space = sh4->memory_if->space;
    sh4->guest->invalid_instr = &sh4_invalid_instr;
    sh4->guest->sq_prefetch = &sh4_ccn_sq_prefetch;
    sh4->guest->sr_updated = &sh4_sr_updated;
    sh4->guest->fpscr_updated = &sh4_fpscr_updated;
    sh4->guest->lookup = &as_lookup;
    sh4->guest->r8 = &as_read8;
    sh4->guest->r16 = &as_read16;
    sh4->guest->r32 = &as_read32;
    sh4->guest->w8 = &as_write8;
    sh4->guest->w16 = &as_write16;
    sh4->guest->w32 = &as_write32;
  }

  sh4->jit = jit_create("sh4", sh4->frontend, sh4->backend,
                        (struct jit_guest *)sh4->guest);

  return 1;
}

void sh4_destroy(struct sh4 *sh4) {
  jit_destroy(sh4->jit);
  sh4_guest_destroy(sh4->guest);
  sh4->frontend->destroy(sh4->frontend);
  sh4->backend->destroy(sh4->backend);

  dc_destroy_memory_interface(sh4->memory_if);
  dc_destroy_execute_interface(sh4->execute_if);
  dc_destroy_debug_interface(sh4->debug_if);
  dc_destroy_device((struct device *)sh4);
}

struct sh4 *sh4_create(struct dreamcast *dc) {
  struct sh4 *sh4 = dc_create_device(dc, sizeof(struct sh4), "sh", &sh4_init,
                                     &sh4_debug_menu);
  sh4->debug_if = dc_create_debug_interface(
      &sh4_dbg_num_registers, &sh4_dbg_step, &sh4_dbg_add_breakpoint,
      &sh4_dbg_remove_breakpoint, &sh4_dbg_read_memory, &sh4_dbg_read_register);
  sh4->execute_if = dc_create_execute_interface(&sh4_run, 0);
  sh4->memory_if = dc_create_memory_interface(dc, &sh4_data_map);

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
  AM_RANGE(0x00000000, 0x001fffff) AM_DEVICE("bios", boot_rom_map)
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
