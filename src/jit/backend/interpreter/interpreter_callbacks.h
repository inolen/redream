#ifndef INTERPRETER_EMIT_H
#define INTERPRETER_EMIT_H

#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/ir/ir_builder.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace interpreter {

union IntValue;
struct IntInstr;

typedef uint32_t (*IntFn)(const IntInstr *instr, uint32_t idx,
                          hw::Memory *memory, IntValue *registers,
                          uint8_t *locals, void *guest_ctx);

IntFn GetCallback(ir::Opcode op, const IntSig &sig, IntAccessMask access_mask);
}
}
}
}

#endif
