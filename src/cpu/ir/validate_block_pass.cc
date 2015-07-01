#include "core/core.h"
#include "cpu/ir/validate_block_pass.h"

using namespace dreavm;
using namespace dreavm::cpu::ir;

void ValidateBlockPass::Run(IRBuilder &builder) {
  for (auto block : builder.blocks()) {
    Instr *tail = block->instrs().tail();
    if (!tail || !IRBuilder::IsTerminator(tail)) {
      builder.Dump();
      LOG(FATAL) << "Block ends in a non-terminating instruction.";
    }
  }
}
