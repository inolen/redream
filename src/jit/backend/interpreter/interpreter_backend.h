#ifndef INTERPRETER_BACKEND_H
#define INTERPRETER_BACKEND_H

#include "jit/backend/backend.h"
#include "jit/backend/interpreter/interpreter_emitter.h"
#include "jit/runtime.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace interpreter {

extern const Register int_registers[];
extern const int int_num_registers;

class InterpreterBackend : public Backend {
 public:
  InterpreterBackend(hw::Memory &memory);

  const Register *registers() const;
  int num_registers() const;

  void Reset();
  std::unique_ptr<RuntimeBlock> AssembleBlock(ir::IRBuilder &builder);

 private:
  InterpreterEmitter emitter_;
};
}
}
}
}

#endif
