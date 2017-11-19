#include "guest/arm7/arm7.h"
#include "core/core.h"
#include "guest/aica/aica.h"
#include "guest/dreamcast.h"
#include "guest/memory.h"
#include "guest/scheduler.h"
#include "imgui.h"
#include "jit/frontend/armv3/armv3_context.h"
#include "jit/frontend/armv3/armv3_fallback.h"
#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/frontend/armv3/armv3_guest.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "stats.h"

#if ARCH_X64
#include "jit/backend/x64/x64_backend.h"
#else
#include "jit/backend/interp/interp_backend.h"
#endif

struct arm7 {
  struct device;

  struct armv3_context ctx;

  /* jit */
  struct jit *jit;
  struct jit_guest *guest;
  struct jit_frontend *frontend;
  struct jit_backend *backend;

  /* interrupts */
  uint32_t requested_interrupts;
};

static void arm7_update_pending_interrupts(struct arm7 *arm);

static void arm7_swap_registers(struct arm7 *arm, int old_mode, int new_mode) {
  if (old_mode == new_mode) {
    return;
  }

  /* store virtual SPSR to banked SPSR for the old mode */
  if (armv3_spsr_table[old_mode]) {
    arm->ctx.r[armv3_spsr_table[old_mode]] = arm->ctx.r[SPSR];
  }

  /* write out active registers to the old mode's bank, and load the
     new mode's bank into the active registers */
  for (int n = 0; n < 16; n++) {
    int old_n = armv3_reg_table[old_mode][n];
    int new_n = armv3_reg_table[new_mode][n];
    uint32_t tmp = arm->ctx.r[n];
    arm->ctx.r[n] = arm->ctx.r[old_n];
    arm->ctx.r[new_n] = arm->ctx.r[old_n];
    arm->ctx.r[old_n] = tmp;
  }

  /* save off pointers to the user bank for the LDM / STM instructions */
  for (int n = 0; n < 16; n++) {
    int new_n = armv3_reg_table[new_mode][n];
    arm->ctx.rusr[n] = &arm->ctx.r[new_n];
  }

  /* load SPSR for the new mode to virtual SPSR */
  if (armv3_spsr_table[new_mode]) {
    arm->ctx.r[SPSR] = arm->ctx.r[armv3_spsr_table[new_mode]];
  }
}

static void arm7_switch_mode(void *data, uint32_t new_sr) {
  struct arm7 *arm = data;
  int old_mode = arm->ctx.r[CPSR] & M_MASK;
  int new_mode = new_sr & M_MASK;

  arm7_swap_registers(arm, old_mode, new_mode);
  arm->ctx.r[SPSR] = arm->ctx.r[CPSR];
  arm->ctx.r[CPSR] = new_sr;

  arm7_update_pending_interrupts(arm);
}

static void arm7_restore_mode(void *data) {
  struct arm7 *arm = data;
  int old_mode = arm->ctx.r[CPSR] & M_MASK;
  int new_mode = arm->ctx.r[SPSR] & M_MASK;

  arm7_swap_registers(arm, old_mode, new_mode);
  arm->ctx.r[CPSR] = arm->ctx.r[SPSR];

  arm7_update_pending_interrupts(arm);
}

static void arm7_update_pending_interrupts(struct arm7 *arm) {
  uint32_t interrupt_mask = 0;

  if (F_CLEAR(arm->ctx.r[CPSR])) {
    interrupt_mask |= ARM7_INT_FIQ;
  }

  arm->ctx.pending_interrupts = arm->requested_interrupts & interrupt_mask;
}

static void arm7_check_interrupts(void *data) {
  struct arm7 *arm = data;

  if (!arm->ctx.pending_interrupts) {
    return;
  }

  if ((arm->ctx.pending_interrupts & ARM7_INT_FIQ)) {
    arm->requested_interrupts &= ~ARM7_INT_FIQ;

    uint32_t newsr = (arm->ctx.r[CPSR] & ~M_MASK);
    newsr |= I_MASK | F_MASK | MODE_FIQ;

    arm7_switch_mode(arm, newsr);
    arm->ctx.r[14] = arm->ctx.r[15] + 4;
    arm->ctx.r[15] = 0x1c;
  }
}

static void arm7_link_code(struct arm7 *arm, void *branch, uint32_t target) {
  jit_link_code(arm->jit, branch, target);
}

static void arm7_compile_code(struct arm7 *arm, uint32_t addr) {
  jit_compile_code(arm->jit, addr);
}

void arm7_mem_write(struct arm7 *arm, uint32_t addr, uint32_t data,
                    uint32_t mask) {
  struct aica *aica = arm->dc->aica;

  if (/*addr >= ARM7_AICA_MEM_BEGIN &&*/ addr <= ARM7_AICA_MEM_END) {
    aica_mem_write(aica, addr, data, mask);
  } else if (addr >= ARM7_AICA_REG_BEGIN && addr <= ARM7_AICA_REG_END) {
    aica_reg_write(aica, addr - ARM7_AICA_REG_BEGIN, data, mask);
  } else {
    LOG_FATAL("arm7_mem_write addr=0x%08x", addr);
  }
}

uint32_t arm7_mem_read(struct arm7 *arm, uint32_t addr, uint32_t mask) {
  struct aica *aica = arm->dc->aica;

  if (/*addr >= ARM7_AICA_MEM_BEGIN &&*/ addr <= ARM7_AICA_MEM_END) {
    return aica_mem_read(aica, addr, mask);
  } else if (addr >= ARM7_AICA_REG_BEGIN && addr <= ARM7_AICA_REG_END) {
    return aica_reg_read(aica, addr - ARM7_AICA_REG_BEGIN, mask);
  } else {
    LOG_FATAL("arm7_mem_read addr=0x%08x", addr);
  }
}

void arm7_raise_interrupt(struct arm7 *arm, enum arm7_interrupt intr) {
  arm->requested_interrupts |= intr;
  arm7_update_pending_interrupts(arm);
}

void arm7_reset(struct arm7 *arm) {
  LOG_INFO("arm7_reset");

  jit_free_code(arm->jit);

  /* reset context */
  memset(&arm->ctx, 0, sizeof(arm->ctx));
  arm->ctx.r[13] = 0x03007f00;
  arm->ctx.r[15] = 0x00000000;
  arm->ctx.r[R13_IRQ] = 0x03007fa0;
  arm->ctx.r[R13_SVC] = 0x03007fe0;
  arm->ctx.r[CPSR] = F_MASK | MODE_SYS;

  arm->runif.running = 1;
}

void arm7_suspend(struct arm7 *arm) {
  arm->runif.running = 0;
}

static void arm7_run(struct device *dev, int64_t ns) {
  struct arm7 *arm = (struct arm7 *)dev;
  static int64_t ARM7_CLOCK_FREQ = INT64_C(20000000);
  int cycles = (int)NANO_TO_CYCLES(ns, ARM7_CLOCK_FREQ);

  jit_run(arm->jit, cycles);

  prof_counter_add(COUNTER_arm7_instrs, arm->ctx.ran_instrs);
}

static void arm7_guest_destroy(struct jit_guest *guest) {
  free((struct armv3_guest *)guest);
}

static struct jit_guest *arm7_guest_create(struct arm7 *arm) {
  struct armv3_guest *guest = calloc(1, sizeof(struct armv3_guest));

  /* dispatch cache */
  guest->addr_mask = 0x001ffffc;

  /* memory interface */
  guest->ctx = &arm->ctx;
  guest->membase = arm7_base(arm->dc->mem);
  guest->mem = arm->dc->mem;
  guest->lookup = &arm7_lookup;
  guest->r8 = &arm7_read8;
  guest->r16 = &arm7_read16;
  guest->r32 = &arm7_read32;
  guest->w8 = &arm7_write8;
  guest->w16 = &arm7_write16;
  guest->w32 = &arm7_write32;

  /* runtime interface */
  guest->data = arm;
  guest->offset_pc = (int)offsetof(struct armv3_context, r[15]);
  guest->offset_cycles = (int)offsetof(struct armv3_context, run_cycles);
  guest->offset_instrs = (int)offsetof(struct armv3_context, ran_instrs);
  guest->offset_interrupts =
      (int)offsetof(struct armv3_context, pending_interrupts);
  guest->compile_code = (jit_compile_cb)&arm7_compile_code;
  guest->link_code = (jit_link_cb)&arm7_link_code;
  guest->check_interrupts = (jit_interrupt_cb)&arm7_check_interrupts;
  guest->switch_mode = (armv3_switch_mode_cb)&arm7_switch_mode;
  guest->restore_mode = (armv3_restore_mode_cb)&arm7_restore_mode;

  return (struct jit_guest *)guest;
}

static int arm7_init(struct device *dev) {
  struct arm7 *arm = (struct arm7 *)dev;

  /* initialize jit */
  arm->guest = arm7_guest_create(arm);
  arm->frontend = armv3_frontend_create(arm->guest);
#if ARCH_X64
  DEFINE_JIT_CODE_BUFFER(arm7_code);
  arm->backend = x64_backend_create(arm->guest, arm7_code, sizeof(arm7_code));
#else
  arm->backend = interp_backend_create(arm->guest, arm->frontend);
#endif
  arm->jit = jit_create("arm7", arm->frontend, arm->backend);

  return 1;
}

#ifdef HAVE_IMGUI
void arm7_debug_menu(struct arm7 *arm) {
  if (igBeginMainMenuBar()) {
    if (igBeginMenu("ARM7", 1)) {
      if (igMenuItem("clear cache", NULL, 0, 1)) {
        jit_invalidate_code(arm->jit);
      }

      igEndMenu();
    }

    igEndMainMenuBar();
  }
}
#endif

void arm7_destroy(struct arm7 *arm) {
  jit_destroy(arm->jit);
  arm7_guest_destroy(arm->guest);
  arm->frontend->destroy(arm->frontend);
  arm->backend->destroy(arm->backend);
  dc_destroy_device((struct device *)arm);
}

struct arm7 *arm7_create(struct dreamcast *dc) {
  struct arm7 *arm =
      dc_create_device(dc, sizeof(struct arm7), "arm", &arm7_init, NULL);

  /* setup run interface */
  arm->runif.enabled = 1;
  arm->runif.run = &arm7_run;

  return arm;
}
