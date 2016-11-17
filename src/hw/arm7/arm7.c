#include <stdbool.h>
#include <stdio.h>
#include "hw/arm7/arm7.h"
#include "core/log.h"
#include "hw/aica/aica.h"
#include "hw/dreamcast.h"
#include "hw/scheduler.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/armv3/armv3_context.h"
#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/jit.h"

#define ARM7_CLOCK_FREQ INT64_C(20000000)

/*
 * arm code layout. executable code sits between 0x00000000 and 0x00800000.
 * each instr is 4 bytes, making for a maximum of 0x00800000 >> 2 blocks
 */
#define ARM7_BLOCK_MASK 0x007fffff
#define ARM7_BLOCK_SHIFT 2
#define ARM7_BLOCK_OFFSET(addr) ((addr & ARM7_BLOCK_MASK) >> ARM7_BLOCK_SHIFT)
#define ARM7_MAX_BLOCKS (0x00800000 >> ARM7_BLOCK_SHIFT)

struct arm7 {
  struct device;

  uint8_t *wave_ram;
  struct armv3_context ctx;
  int pending_interrupts;

  // jit
  struct armv3_guest guest;
  struct jit_frontend *frontend;
  struct jit_backend *backend;
  struct jit *jit;
};

struct arm7 *g_arm;

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

static void arm7_switch_mode(void *data, uint64_t mode64) {
  struct arm7 *arm = data;
  int old_mode = arm->ctx.r[CPSR] & M_MASK;
  int new_mode = (int)mode64;

  arm7_swap_registers(arm, old_mode, new_mode);
  arm->ctx.r[SPSR] = arm->ctx.r[CPSR];
  arm->ctx.r[CPSR] = (arm->ctx.r[CPSR] & ~M_MASK) | new_mode;
}

static void arm7_restore_mode(void *data) {
  struct arm7 *arm = data;
  int old_mode = arm->ctx.r[CPSR] & M_MASK;
  int new_mode = arm->ctx.r[SPSR] & M_MASK;

  arm7_swap_registers(arm, old_mode, new_mode);
  arm->ctx.r[CPSR] = arm->ctx.r[SPSR];
}

static void arm7_software_interrupt(void *data) {
  struct arm7 *arm = data;

  arm7_switch_mode(arm, MODE_SVC);
  arm->ctx.r[CPSR] |= I_MASK;
  arm->ctx.r[14] = arm->ctx.r[15] + 4;
  arm->ctx.r[15] = 0x08;
}

static void arm7_check_interrupts(struct arm7 *arm) {
  if (!arm->pending_interrupts) {
    return;
  }

  if ((arm->pending_interrupts & ARM7_INT_FIQ)) {
    if (F_CLEAR(arm->ctx.r[CPSR])) {
      arm->pending_interrupts &= ~ARM7_INT_FIQ;

      arm7_switch_mode(arm, MODE_FIQ);
      arm->ctx.r[CPSR] |= I_MASK | F_MASK;
      arm->ctx.r[14] = arm->ctx.r[15] + 4;
      arm->ctx.r[15] = 0x1c;
    }
  }
}

void arm7_raise_interrupt(struct arm7 *arm, enum arm7_interrupt intr) {
  arm->pending_interrupts |= intr;
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
  int flags = 0;
  code_pointer_t code = jit_compile_code(g_arm->jit, guest_addr, flags);
  code();
}

static inline code_pointer_t arm7_get_code(struct arm7 *arm, uint32_t addr) {
  int offset = ARM7_BLOCK_OFFSET(addr);
  DCHECK_LT(offset, ARM7_MAX_BLOCKS);
  return arm->jit->code[offset];
}

static void arm7_run(struct device *dev, int64_t ns) {
  struct arm7 *arm = (struct arm7 *)dev;

  int64_t cycles = MAX(NANO_TO_CYCLES(ns, ARM7_CLOCK_FREQ), INT64_C(1));
  arm->ctx.num_cycles = (int)cycles;

  g_arm = arm;

  while (arm->ctx.num_cycles > 0) {
    code_pointer_t code = arm7_get_code(arm, arm->ctx.r[15]);
    code();

    arm7_check_interrupts(arm);
  }

  g_arm = NULL;
}

static bool arm7_init(struct device *dev) {
  struct arm7 *arm = (struct arm7 *)dev;
  struct dreamcast *dc = arm->dc;

  arm->wave_ram = memory_translate(dc->memory, "aica wave ram", 0x00000000);

  /* initialize jit interface */
  arm->guest.block_mask = ARM7_BLOCK_MASK;
  arm->guest.block_shift = ARM7_BLOCK_SHIFT;
  arm->guest.block_max = ARM7_MAX_BLOCKS;
  arm->guest.ctx_base = &arm->ctx;
  arm->guest.mem_base = arm->memory_if->space->base;
  arm->guest.mem_self = arm->memory_if->space;
  arm->guest.r8 = &as_read8;
  arm->guest.r16 = &as_read16;
  arm->guest.r32 = &as_read32;
  arm->guest.w8 = &as_write8;
  arm->guest.w16 = &as_write16;
  arm->guest.w32 = &as_write32;
  arm->guest.ctx = &arm->ctx;
  arm->guest.self = arm;
  arm->guest.switch_mode = &arm7_switch_mode;
  arm->guest.restore_mode = &arm7_restore_mode;
  arm->guest.software_interrupt = &arm7_software_interrupt;

  arm->frontend = armv3_frontend_create(&arm->guest);
  arm->backend = x64_backend_create((struct jit_guest *)&arm->guest);
  arm->jit = jit_create((struct jit_guest *)&arm->guest, arm->frontend,
                        arm->backend, &arm7_compile_pc);

  return true;
}

void arm7_destroy(struct arm7 *arm) {
  if (arm->jit) {
    jit_destroy(arm->jit);
  }

  if (arm->backend) {
    x64_backend_destroy(arm->backend);
  }

  if (arm->backend) {
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
