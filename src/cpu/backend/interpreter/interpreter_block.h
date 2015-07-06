#ifndef INTERPRETER_BLOCK_H
#define INTERPRETER_BLOCK_H

#include "cpu/ir/ir_builder.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

struct IntInstr;
union Register;

class InterpreterBlock : public RuntimeBlock {
 public:
  InterpreterBlock(int guest_cycles, IntInstr *instrs, int num_instrs,
                   int num_registers);
  ~InterpreterBlock();

  uint32_t Call(emu::Memory *memory, void *guest_ctx);

 private:
  IntInstr *instrs_;
  int num_instrs_;
  int num_registers_;
};
}
}
}
}

#endif
