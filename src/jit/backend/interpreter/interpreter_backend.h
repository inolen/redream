#ifndef INTERPRETER_BACKEND_H
#define INTERPRETER_BACKEND_H

#include "jit/backend/backend.h"
#include "jit/backend/interpreter/interpreter_emitter.h"
#include "jit/runtime.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace interpreter {

enum { NUM_INT_REGISTERS = 8, MAX_INT_STACK = 4096 };

// fake set of virtual registers used by the interpreter
extern const Register int_registers[];
extern const int int_num_registers;

// global interpreter state
struct InterpreterState {
  IntValue r[NUM_INT_REGISTERS];
  uint8_t stack[MAX_INT_STACK];
  uint32_t pc, sp;
};
extern InterpreterState int_state;

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
