#ifndef INTERPRETER_BLOCK_H
#define INTERPRETER_BLOCK_H

#include "cpu/ir/ir_builder.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

struct IntInstr;

class InterpreterBlock : public RuntimeBlock {
 public:
  InterpreterBlock(int guest_cycles, IntInstr *instrs, int num_instrs,
                   int locals_size);
  ~InterpreterBlock();

  uint32_t Call(emu::Memory *memory, void *guest_ctx);
  void Dump();

 private:
  IntInstr *instrs_;
  int num_instrs_;
  int locals_size_;
};
}
}
}
}

#endif
