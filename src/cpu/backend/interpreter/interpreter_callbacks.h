#ifndef INTERPRETER_EMIT_H
#define INTERPRETER_EMIT_H

#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/ir/ir_builder.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

IntFn GetCallback(ir::Opcode op, const IntSig &sig, IntAccessMask access_mask);
}
}
}
}

#endif
