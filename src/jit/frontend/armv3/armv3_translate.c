#include "jit/frontend/armv3/armv3_translate.h"
#include "core/assert.h"
#include "core/profiler.h"
#include "hw/arm7/arm7.h"
#include "jit/frontend/armv3/armv3_context.h"
#include "jit/frontend/armv3/armv3_disasm.h"
#include "jit/frontend/armv3/armv3_fallback.h"
#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/ir/ir.h"

void armv3_translate(const struct armv3_guest *guest, uint32_t addr, int size,
                     int flags, struct ir *ir) {
  int cycles = 0;

  for (int i = 0; i < size; i += 4) {
    uint32_t data = guest->r32(guest->mem_self, addr);
    void *fallback = armv3_fallback(data);

    ir_call_fallback(ir, fallback, addr, data);

    // TODO better cycle tracking
    cycles++;

    addr += 4;
  }

  // update remaining cycles
  struct ir_value *remaining_cycles = ir_load_context(
      ir, offsetof(struct armv3_context, remaining_cycles), VALUE_I32);
  remaining_cycles = ir_sub(ir, remaining_cycles, ir_alloc_i32(ir, cycles));
  ir_store_context(ir, offsetof(struct armv3_context, remaining_cycles),
                   remaining_cycles);

  // update num instructions
  struct ir_value *num_instrs_ptr =
      ir_alloc_i64(ir, (uint64_t)&STAT_arm7_instrs);
  struct ir_value *num_instrs = ir_load(ir, num_instrs_ptr, VALUE_I64);
  num_instrs = ir_add(ir, num_instrs, ir_alloc_i64(ir, size / 4));
  ir_store(ir, num_instrs_ptr, num_instrs);
}
