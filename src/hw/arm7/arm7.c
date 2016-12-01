#include <stdbool.h>
#include <stdio.h>
#include "hw/arm7/arm7.h"
#include "core/log.h"
#include "hw/aica/aica.h"
#include "hw/dreamcast.h"
#include "hw/scheduler.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/armv3/armv3_analyze.h"
#include "jit/frontend/armv3/armv3_context.h"
#include "jit/frontend/armv3/armv3_disasm.h"
#include "jit/frontend/armv3/armv3_fallback.h"
#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"

DEFINE_AGGREGATE_COUNTER(arm7_instrs);

#define ARM7_CLOCK_FREQ INT64_C(20000000)

/*
 * arm code layout. executable code sits between 0x00000000 and 0x00800000.
 * each instr is 4 bytes, making for a maximum of 0x00800000 >> 2 blocks
 */
#define ARM7_BLOCK_MASK 0x007fffff
#define ARM7_BLOCK_SHIFT 2
#define ARM7_BLOCK_OFFSET(addr) ((addr & ARM7_BLOCK_MASK) >> ARM7_BLOCK_SHIFT)
#define ARM7_MAX_BLOCKS (0x00800000 >> ARM7_BLOCK_SHIFT)

static uint8_t arm7_code[1024 * 1024 * 8];
static int arm7_code_size = 1024 * 1024 * 8;
static int arm7_stack_size = 1024;

struct arm7 {
  struct device;

  uint8_t *wave_ram;
  struct armv3_context ctx;

  /* jit */
  struct jit_frontend *frontend;
  struct jit_backend *backend;
  struct jit *jit;

  /* interrupts */
  uint32_t requested_interrupts;
};

struct arm7 *g_arm;

static void arm7_update_pending_interrupts(struct arm7 *arm);

static void arm7_swap_registers(struct arm7 *arm, int old_mode, int new_mode) {
  if (old_mode == new_mode) {
    return;
  }

  /* store virtual SPSR to banked SPSR for the old mode */
  if (armv3_spsr_table[old_mode]) {
    arm->ctx.r[armv3_spsr_table[old_mode]] = arm->ctx.r[SPSR];
  }

  /*
   * write out active registers to the old mode's bank, and load the
   * new mode's bank into the active registers
   */
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

static void arm7_switch_mode(void *data, uint64_t sr) {
  struct arm7 *arm = data;
  uint32_t new_sr = (uint32_t)sr;
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
  /* unlink stale blocks */
  jit_unlink_blocks(arm->jit);

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

static void arm7_compile_pc() {
  uint32_t guest_addr = g_arm->ctx.r[15];
  uint8_t *guest_ptr = as_translate(g_arm->memory_if->space, guest_addr);
  code_pointer_t code = jit_compile_code(g_arm->jit, guest_addr);
  code();
}

static inline code_pointer_t arm7_get_code(struct arm7 *arm, uint32_t addr) {
  int offset = ARM7_BLOCK_OFFSET(addr);
  DCHECK_LT(offset, ARM7_MAX_BLOCKS);
  return arm->jit->code[offset];
}

static void arm7_translate(void *data, uint32_t addr, struct ir *ir,
                           int flags) {
  struct arm7 *arm = data;

  int size;
  armv3_analyze_block(arm->jit, addr, &flags, &size);

  /* update remaining cycles */
  struct ir_value *remaining_cycles = ir_load_context(
      ir, offsetof(struct armv3_context, remaining_cycles), VALUE_I32);
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
    uint32_t data = arm->jit->r32(arm->jit->space, addr + i);
    void *fallback = armv3_fallback(data);
    ir_call_fallback(ir, fallback, addr + i, data);
  }
}

static void arm7_run(struct device *dev, int64_t ns) {
  PROF_ENTER("cpu", "arm7_run");

  struct arm7 *arm = (struct arm7 *)dev;

  int64_t cycles = MAX(NANO_TO_CYCLES(ns, ARM7_CLOCK_FREQ), INT64_C(1));
  arm->ctx.remaining_cycles = (int)cycles;
  arm->ctx.ran_instrs = 0;

  g_arm = arm;

  while (arm->ctx.remaining_cycles > 0) {
    code_pointer_t code = arm7_get_code(arm, arm->ctx.r[15]);
    code();
    arm7_check_pending_interrupts(arm);
  }

  g_arm = NULL;

  prof_counter_add(COUNTER_arm7_instrs, arm->ctx.ran_instrs);

  PROF_LEAVE();
}

static bool arm7_init(struct device *dev) {
  struct arm7 *arm = (struct arm7 *)dev;
  struct dreamcast *dc = arm->dc;

  /* initialize jit and its interfaces */
  struct jit *jit = jit_create("arm7");
  jit->block_mask = ARM7_BLOCK_MASK;
  jit->block_shift = ARM7_BLOCK_SHIFT;
  jit->block_max = ARM7_MAX_BLOCKS;
  jit->ctx = &arm->ctx;
  jit->mem = arm->memory_if->space->base;
  jit->space = arm->memory_if->space;
  jit->r8 = &as_read8;
  jit->r16 = &as_read16;
  jit->r32 = &as_read32;
  jit->w8 = &as_write8;
  jit->w16 = &as_write16;
  jit->w32 = &as_write32;
  arm->jit = jit;

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

  if (!jit_init(arm->jit, arm->frontend, arm->backend, &arm7_compile_pc)) {
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

// clang-format off
AM_BEGIN(struct arm7, arm7_data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_data_map)
  AM_RANGE(0x00800000, 0x00810fff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_reg_map)
AM_END();
// clang-format on
