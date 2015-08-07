#include "core/core.h"
#include "cpu/ir/passes/validate_block_pass.h"
#include "emu/profiler.h"

using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;

void ValidateBlockPass::Run(IRBuilder &builder) {
  PROFILER_SCOPE("runtime", "ValidateBlockPass::Run");

  for (auto block : builder.blocks()) {
    Instr *tail = block->instrs().tail();

    if (!tail || !IRBuilder::IsTerminator(tail)) {
      builder.Dump();

      LOG(FATAL) << "Block ends in a non-terminating instruction.";
    }
  }
}
