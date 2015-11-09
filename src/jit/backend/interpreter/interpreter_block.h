#ifndef INTERPRETER_BLOCK_H
#define INTERPRETER_BLOCK_H

#include "hw/memory.h"
#include "jit/backend/interpreter/interpreter_emitter.h"
#include "jit/runtime.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace interpreter {

struct InterpreterBlock : public RuntimeBlock {
  InterpreterBlock(int guest_cycles, IntInstr *instrs, int num_instrs,
                   int locals_size);

  static uint32_t Call(RuntimeBlock *block);

  IntInstr *instrs_;
  int num_instrs_;
  int locals_size_;
};
}
}
}
}

#endif
