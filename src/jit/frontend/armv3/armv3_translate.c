#include "jit/frontend/armv3/armv3_translate.h"
#include "jit/frontend/armv3/armv3_fallback.h"
#include "jit/ir/ir.h"

void armv3_emit_instr(struct armv3_frontend *frontend, struct ir *ir, int flags,
                      uint32_t addr, uint32_t instr) {
  void *fallback = armv3_fallback(instr);
  ir_fallback(ir, fallback, addr, instr);
}
