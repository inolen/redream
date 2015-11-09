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
extern uint32_t int_nextpc;

class InterpreterBackend : public Backend {
 public:
  InterpreterBackend(hw::Memory &memory);

  const Register *registers() const;
  int num_registers() const;

  void Reset();
  RuntimeBlock *AssembleBlock(ir::IRBuilder &builder, void *guest_ctx);
  void DumpBlock(RuntimeBlock *block);
  void FreeBlock(RuntimeBlock *block);

  bool HandleException(sys::Exception &ex);

 private:
  InterpreterEmitter emitter_;
};
}
}
}
}

#endif
