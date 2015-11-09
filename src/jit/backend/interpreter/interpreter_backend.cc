#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/interpreter/interpreter_block.h"
#include "jit/backend/interpreter/interpreter_emitter.h"
#include "sys/exception_handler.h"

using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend;
using namespace dreavm::jit::backend::interpreter;
using namespace dreavm::jit::ir;
using namespace dreavm::sys;

namespace dreavm {
namespace jit {
namespace backend {
namespace interpreter {
const Register int_registers[] = {
    {"a", ir::VALUE_INT_MASK},   {"b", ir::VALUE_INT_MASK},
    {"c", ir::VALUE_INT_MASK},   {"d", ir::VALUE_INT_MASK},
    {"e", ir::VALUE_FLOAT_MASK}, {"f", ir::VALUE_FLOAT_MASK},
    {"g", ir::VALUE_FLOAT_MASK}, {"h", ir::VALUE_FLOAT_MASK}};

const int int_num_registers = sizeof(int_registers) / sizeof(Register);

uint32_t int_nextpc = 0;
}
}
}
}

InterpreterBackend::InterpreterBackend(Memory &memory)
    : Backend(memory), emitter_(memory) {}

const Register *InterpreterBackend::registers() const { return int_registers; }

int InterpreterBackend::num_registers() const { return int_num_registers; }

void InterpreterBackend::Reset() { emitter_.Reset(); }

RuntimeBlock *InterpreterBackend::AssembleBlock(ir::IRBuilder &builder,
                                                void *guest_ctx) {
  IntInstr *instr;
  int num_instr;
  int locals_size;

  if (!emitter_.Emit(builder, guest_ctx, &instr, &num_instr, &locals_size)) {
    return nullptr;
  }

  return new InterpreterBlock(builder.guest_cycles(), instr, num_instr,
                              locals_size);
}

void InterpreterBackend::DumpBlock(RuntimeBlock *block) {
  LOG_WARNING("Not implemented");
}

void InterpreterBackend::FreeBlock(RuntimeBlock *block) { delete block; }

bool InterpreterBackend::HandleException(Exception &ex) { return false; }
