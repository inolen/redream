#include <stdbool.h>
#include <stdio.h>
#include "hw/arm7/arm7.h"
#include "core/log.h"
#include "hw/aica/aica.h"
#include "hw/arm7/x64/arm7_dispatch.h"
#include "hw/dreamcast.h"
#include "hw/scheduler.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/armv3/armv3_analyze.h"
#include "jit/frontend/armv3/armv3_context.h"
#include "jit/frontend/armv3/armv3_disasm.h"
#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/frontend/armv3/armv3_translate.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"

DEFINE_AGGREGATE_COUNTER(arm7_instrs);

struct arm7 {
  struct device;

  uint8_t *wave_ram;
  struct armv3_context ctx;

  /* jit */
  struct jit *jit;
  struct jit_guest guest;
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
  for (int i = 0; i < 7; i++) {
    int n = 8 + i;
    int old_n = armv3_reg_table[old_mode][i];
    int new_n = armv3_reg_table[new_mode][i];
    uint32_t tmp = arm->ctx.r[n];
    arm->ctx.r[n] = arm->ctx.r[old_n];
    arm->ctx.r[new_n] = arm->ctx.r[old_n];
    arm->ctx.r[old_n] = tmp;
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

static void arm7_software_interrupt(void *data) {
  struct arm7 *arm = data;

  uint32_t newsr = (arm->ctx.r[CPSR] & ~M_MASK);
  newsr |= I_MASK | MODE_SVC;

  arm7_switch_mode(arm, newsr);
  arm->ctx.r[14] = arm->ctx.r[15] + 4;
  arm->ctx.r[15] = 0x08;
}

static void arm7_update_pending_interrupts(struct arm7 *arm) {
  uint32_t interrupt_mask = 0;

  if (F_CLEAR(arm->ctx.r[CPSR])) {
    interrupt_mask |= ARM7_INT_FIQ;
  }

  arm->ctx.pending_interrupts = arm->requested_interrupts & interrupt_mask;
}

void arm7_check_pending_interrupts(struct arm7 *arm) {
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

void arm7_raise_interrupt(struct arm7 *arm, enum arm7_interrupt intr) {
  arm->requested_interrupts |= intr;
  arm7_update_pending_interrupts(arm);
}

void arm7_reset(struct arm7 *arm) {
  LOG_INFO("arm7_reset");

  jit_free_blocks(arm->jit);

  /* reset context */
  memset(&arm->ctx, 0, sizeof(arm->ctx));
  arm->ctx.r[13] = 0x03007f00;
  arm->ctx.r[15] = 0x00000000;
  arm->ctx.r[R13_IRQ] = 0x03007fa0;
  arm->ctx.r[R13_SVC] = 0x03007fe0;
  arm->ctx.r[CPSR] = F_MASK | MODE_SYS;

  arm->execute_if->running = 1;
}

void arm7_suspend(struct arm7 *arm) {
  arm->execute_if->running = 0;
}

static void arm7_translate(void *data, uint32_t addr, struct ir *ir, int flags,
                           int *out_size) {
  struct arm7 *arm = data;

  int size;
  armv3_analyze_block(arm->jit, addr, &flags, &size);

  /* cycle check */
  struct ir_value *remaining_cycles = ir_load_context(
      ir, offsetof(struct armv3_context, remaining_cycles), VALUE_I32);
  struct ir_value *done = ir_cmp_sle(ir, remaining_cycles, ir_alloc_i32(ir, 0));
  ir_branch_true(ir, ir_alloc_ptr(ir, arm7_dispatch_leave), done);

  /* interrupt check */
  struct ir_value *pending_intr = ir_load_context(
      ir, offsetof(struct armv3_context, pending_interrupts), VALUE_I32);
  ir_branch_true(ir, ir_alloc_ptr(ir, arm7_dispatch_interrupt), pending_intr);

  /* update remaining cycles */
  int cycles = (size / 4);
  remaining_cycles = ir_sub(ir, remaining_cycles, ir_alloc_i32(ir, cycles));
  ir_store_context(ir, offsetof(struct armv3_context, remaining_cycles),
                   remaining_cycles);
  CHECK(cycles && size);

  /* update instruction run count */
  struct ir_value *ran_instrs = ir_load_context(
      ir, offsetof(struct armv3_context, ran_instrs), VALUE_I64);
  ran_instrs = ir_add(ir, ran_instrs, ir_alloc_i64(ir, size / 4));
  ir_store_context(ir, offsetof(struct armv3_context, ran_instrs), ran_instrs);

  /* emit fallbacks */
  for (int i = 0; i < size; i += 4) {
    uint32_t data = as_read32(arm->memory_if->space, addr + i);
    armv3_emit_instr((struct armv3_frontend *)arm->frontend, ir, 0, addr + i,
                     data);
  }

  ir_branch(ir, ir_alloc_ptr(ir, arm7_dispatch_dynamic));

  /* return size */
  *out_size = size;
}

static void arm7_run(struct device *dev, int64_t ns) {
  PROF_ENTER("cpu", "arm7_run");

  struct arm7 *arm = (struct arm7 *)dev;

  static int64_t ARM7_CLOCK_FREQ = INT64_C(20000000);
  int64_t cycles = NANO_TO_CYCLES(ns, ARM7_CLOCK_FREQ);
  arm->ctx.remaining_cycles = (int)cycles;
  arm->ctx.ran_instrs = 0;
  arm7_dispatch_enter(arm, &arm->ctx, arm->memory_if->space->base);
  prof_counter_add(COUNTER_arm7_instrs, arm->ctx.ran_instrs);

  PROF_LEAVE();
}

static bool arm7_init(struct device *dev) {
  struct arm7 *arm = (struct arm7 *)dev;
  struct dreamcast *dc = arm->dc;

  /* initialize jit and its interfaces */
  arm->jit = jit_create("arm7");

  arm7_dispatch_init(arm, arm->jit, &arm->ctx, arm->memory_if->space->base);

  struct jit_guest *guest = &arm->guest;
  guest->ctx = &arm->ctx;
  guest->mem = arm->memory_if->space->base;
  guest->space = arm->memory_if->space;
  guest->lookup_code = &arm7_dispatch_lookup_code;
  guest->cache_code = &arm7_dispatch_cache_code;
  guest->invalidate_code = &arm7_dispatch_invalidate_code;
  guest->patch_edge = &arm7_dispatch_patch_edge;
  guest->restore_edge = &arm7_dispatch_restore_edge;
  guest->r8 = &as_read8;
  guest->r16 = &as_read16;
  guest->r32 = &as_read32;
  guest->w8 = &as_write8;
  guest->w16 = &as_write16;
  guest->w32 = &as_write32;

  struct armv3_frontend *frontend =
      (struct armv3_frontend *)armv3_frontend_create(arm->jit);
  frontend->data = arm;
  frontend->translate = &arm7_translate;
  frontend->switch_mode = &arm7_switch_mode;
  frontend->restore_mode = &arm7_restore_mode;
  frontend->software_interrupt = &arm7_software_interrupt;
  arm->frontend = (struct jit_frontend *)frontend;

  struct jit_backend *backend =
      x64_backend_create(arm->jit, arm7_code, arm7_code_size, arm7_stack_size);
  arm->backend = backend;

  if (!jit_init(arm->jit, &arm->guest, arm->frontend, arm->backend)) {
    return false;
  }

  arm->wave_ram = memory_translate(dc->memory, "aica wave ram", 0x00000000);

  return true;
}

void arm7_destroy(struct arm7 *arm) {
  if (arm->jit) {
    jit_destroy(arm->jit);
  }

  if (arm->backend) {
    x64_backend_destroy(arm->backend);
  }

  if (arm->frontend) {
    armv3_frontend_destroy(arm->frontend);
  }

  dc_destroy_memory_interface(arm->memory_if);
  dc_destroy_execute_interface(arm->execute_if);
  dc_destroy_device((struct device *)arm);
}

struct arm7 *arm7_create(struct dreamcast *dc) {
  struct arm7 *arm =
      dc_create_device(dc, sizeof(struct arm7), "arm", &arm7_init);
  arm->execute_if = dc_create_execute_interface(&arm7_run, 0);
  arm->memory_if = dc_create_memory_interface(dc, &arm7_data_map);

  return arm;
}

/* clang-format off */
AM_BEGIN(struct arm7, arm7_data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_data_map)
  AM_RANGE(0x00800000, 0x00810fff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_reg_map)
AM_END();
/* clang-format on */
