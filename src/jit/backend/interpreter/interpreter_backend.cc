#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/interpreter/interpreter_block.h"
#include "jit/backend/interpreter/interpreter_emitter.h"
#include "sys/exception_handler.h"

using namespace dvm;
using namespace dvm::hw;
using namespace dvm::jit;
using namespace dvm::jit::backend;
using namespace dvm::jit::backend::interpreter;
using namespace dvm::jit::ir;
using namespace dvm::sys;

namespace dvm {
namespace jit {
namespace backend {
namespace interpreter {
const Register int_registers[NUM_INT_REGISTERS] = {
    {"ia", ir::VALUE_INT_MASK},   {"ib", ir::VALUE_INT_MASK},
    {"ic", ir::VALUE_INT_MASK},   {"id", ir::VALUE_INT_MASK},
    {"ie", ir::VALUE_INT_MASK},   {"if", ir::VALUE_INT_MASK},
    {"ig", ir::VALUE_INT_MASK},   {"ih", ir::VALUE_INT_MASK},
    {"ii", ir::VALUE_INT_MASK},   {"ij", ir::VALUE_INT_MASK},
    {"ik", ir::VALUE_INT_MASK},   {"il", ir::VALUE_INT_MASK},
    {"im", ir::VALUE_INT_MASK},   {"in", ir::VALUE_INT_MASK},
    {"io", ir::VALUE_INT_MASK},   {"ip", ir::VALUE_INT_MASK},
    {"fa", ir::VALUE_FLOAT_MASK}, {"fb", ir::VALUE_FLOAT_MASK},
    {"fc", ir::VALUE_FLOAT_MASK}, {"fd", ir::VALUE_FLOAT_MASK},
    {"fe", ir::VALUE_FLOAT_MASK}, {"ff", ir::VALUE_FLOAT_MASK},
    {"fg", ir::VALUE_FLOAT_MASK}, {"fh", ir::VALUE_FLOAT_MASK},
    {"fi", ir::VALUE_FLOAT_MASK}, {"fj", ir::VALUE_FLOAT_MASK},
    {"fk", ir::VALUE_FLOAT_MASK}, {"fl", ir::VALUE_FLOAT_MASK},
    {"fm", ir::VALUE_FLOAT_MASK}, {"fn", ir::VALUE_FLOAT_MASK},
    {"fo", ir::VALUE_FLOAT_MASK}, {"fp", ir::VALUE_FLOAT_MASK}};

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
                                               SourceMap &source_map,
                                               void *guest_ctx,
                                               int block_flags) {
  int idx = int_num_blocks++;
  if (idx >= MAX_INT_BLOCKS) {
    return nullptr;
  }

  InterpreterBlock *block = &int_blocks[idx];
  if (!emitter_.Emit(builder, guest_ctx, source_map, &block->instrs,
                     &block->num_instrs, &block->locals_size)) {
    return nullptr;
  }

  return int_runners[idx];
}

bool InterpreterBackend::HandleException(BlockPointer block, int *block_flags,
                                         Exception &ex) {
  return false;
}
