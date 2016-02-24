#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/interpreter/interpreter_block.h"
#include "jit/backend/interpreter/interpreter_emitter.h"
#include "sys/exception_handler.h"

using namespace re;
using namespace re::hw;
using namespace re::jit;
using namespace re::jit::backend;
using namespace re::jit::backend::interpreter;
using namespace re::jit::ir;
using namespace re::sys;

namespace re {
namespace jit {
namespace backend {
namespace interpreter {
const Register int_registers[NUM_INT_REGISTERS] = {
    {"ia", ir::VALUE_INT_MASK, nullptr},
    {"ib", ir::VALUE_INT_MASK, nullptr},
    {"ic", ir::VALUE_INT_MASK, nullptr},
    {"id", ir::VALUE_INT_MASK, nullptr},
    {"ie", ir::VALUE_INT_MASK, nullptr},
    {"if", ir::VALUE_INT_MASK, nullptr},
    {"ig", ir::VALUE_INT_MASK, nullptr},
    {"ih", ir::VALUE_INT_MASK, nullptr},
    {"ii", ir::VALUE_INT_MASK, nullptr},
    {"ij", ir::VALUE_INT_MASK, nullptr},
    {"ik", ir::VALUE_INT_MASK, nullptr},
    {"il", ir::VALUE_INT_MASK, nullptr},
    {"im", ir::VALUE_INT_MASK, nullptr},
    {"in", ir::VALUE_INT_MASK, nullptr},
    {"io", ir::VALUE_INT_MASK, nullptr},
    {"ip", ir::VALUE_INT_MASK, nullptr},
    {"fa", ir::VALUE_FLOAT_MASK, nullptr},
    {"fb", ir::VALUE_FLOAT_MASK, nullptr},
    {"fc", ir::VALUE_FLOAT_MASK, nullptr},
    {"fd", ir::VALUE_FLOAT_MASK, nullptr},
    {"fe", ir::VALUE_FLOAT_MASK, nullptr},
    {"ff", ir::VALUE_FLOAT_MASK, nullptr},
    {"fg", ir::VALUE_FLOAT_MASK, nullptr},
    {"fh", ir::VALUE_FLOAT_MASK, nullptr},
    {"fi", ir::VALUE_FLOAT_MASK, nullptr},
    {"fj", ir::VALUE_FLOAT_MASK, nullptr},
    {"fk", ir::VALUE_FLOAT_MASK, nullptr},
    {"fl", ir::VALUE_FLOAT_MASK, nullptr},
    {"fm", ir::VALUE_FLOAT_MASK, nullptr},
    {"fn", ir::VALUE_FLOAT_MASK, nullptr},
    {"fo", ir::VALUE_FLOAT_MASK, nullptr},
    {"fp", ir::VALUE_FLOAT_MASK, nullptr}};

const int int_num_registers = sizeof(int_registers) / sizeof(Register);

InterpreterState int_state;
}
}
}
}

InterpreterBackend::InterpreterBackend(Memory &memory)
    : Backend(memory), emitter_(memory) {}

const Register *InterpreterBackend::registers() const { return int_registers; }

int InterpreterBackend::num_registers() const { return int_num_registers; }

void InterpreterBackend::Reset() {
  int_num_blocks = 0;
  emitter_.Reset();
}

BlockPointer InterpreterBackend::AssembleBlock(ir::IRBuilder &builder,
                                               void *guest_ctx,
                                               int block_flags) {
  int idx = int_num_blocks++;
  if (idx >= MAX_INT_BLOCKS) {
    return nullptr;
  }

  InterpreterBlock *block = &int_blocks[idx];
  if (!emitter_.Emit(builder, guest_ctx, &block->instrs, &block->num_instrs,
                     &block->locals_size)) {
    return nullptr;
  }

  return int_runners[idx];
}

bool InterpreterBackend::HandleFastmemException(Exception &ex) { return false; }
