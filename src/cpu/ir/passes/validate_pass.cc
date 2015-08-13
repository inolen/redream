#include "core/core.h"
#include "cpu/ir/passes/validate_pass.h"
#include "emu/profiler.h"

using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;

void ValidatePass::Run(IRBuilder &builder) {
  PROFILER_RUNTIME("ValidatePass::Run");

  for (auto block : builder.blocks()) {
    ValidateBlock(builder, block);
  }
}

void ValidatePass::ValidateBlock(IRBuilder &builder, Block *block) {
  Instr *tail = block->instrs().tail();

  CHECK(tail && IRBuilder::IsTerminator(tail))
      << "Block ends in a non-terminating instruction";

  for (auto instr : block->instrs()) {
    ValidateInstr(builder, block, instr);
  }
}

void ValidatePass::ValidateInstr(IRBuilder &builder, Block *block,
                                 Instr *instr) {
  Value *result = instr->result();

  if (result) {
    for (auto ref : result->refs()) {
      CHECK_EQ(ref->instr()->block(), block)
          << "Instruction result is referenced by multiple blocks, values can "
             "only be used in the block they're declared in";
    }
  }
}
