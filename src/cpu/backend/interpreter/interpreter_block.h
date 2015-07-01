#ifndef INTERPRETER_BLOCK_H
#define INTERPRETER_BLOCK_H

#include <memory>
#include "cpu/ir/ir_builder.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

struct Instr;
union Register;

class InterpreterBlock : public RuntimeBlock {
 public:
  InterpreterBlock(int guest_cycles, Instr *instrs, int num_instrs,
                   int num_registers);
  ~InterpreterBlock();

  uint32_t Call(RuntimeContext &runtime_ctx);

 private:
  Instr *instrs_;
  int num_instrs_;
  int num_registers_;
};
}
}
}
}

#endif
