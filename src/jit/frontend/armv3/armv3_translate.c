#include "jit/frontend/armv3/armv3_translate.h"
#include "core/assert.h"
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
  struct ir_value *num_cycles = ir_load_context(
      ir, offsetof(struct armv3_context, num_cycles), VALUE_I32);
  num_cycles = ir_sub(ir, num_cycles, ir_alloc_i32(ir, cycles));
  ir_store_context(ir, offsetof(struct armv3_context, num_cycles), num_cycles);
}
