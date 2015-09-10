#ifndef SH4_EMIT_H
#define SH4_EMIT_H

#include "jit/frontend/sh4/sh4_builder.h"

namespace dreavm {
namespace jit {
namespace frontend {
namespace sh4 {

typedef void (*EmitCallback)(SH4Builder &b, const FPUState &, const Instr &i);

#define SH4_INSTR(name, instr_code, cycles, flags) \
  void Emit_OP_##name(SH4Builder &b, const FPUState &fpu, const Instr &instr);
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR

extern EmitCallback emit_callbacks[NUM_OPCODES];
}
}
}
}

#endif
