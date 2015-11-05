#include "core/core.h"
#include "emu/profiler.h"
#include "jit/ir/passes/validate_pass.h"

using namespace dreavm::jit::ir;
using namespace dreavm::jit::ir::passes;

void ValidatePass::Run(IRBuilder &builder) {
  PROFILER_RUNTIME("ValidatePass::Run");

  for (auto block : builder.blocks()) {
    ValidateBlock(builder, block);
  }
}

void ValidatePass::ValidateBlock(IRBuilder &builder, Block *block) {
  Instr *tail = block->instrs().tail();

  CHECK(tail && IRBuilder::IsBranch(tail),
        "Block ends in a non-branch instruction");

  for (auto instr : block->instrs()) {
    CHECK(!IRBuilder::IsBranch(instr) || instr == tail,
          "Block contains a branch instruction before its end");

    ValidateInstr(builder, block, instr);
  }
}

void ValidatePass::ValidateInstr(IRBuilder &builder, Block *block,
                                 Instr *instr) {
  Value *result = instr->result();

  if (result) {
    for (auto ref : result->refs()) {
      CHECK_EQ(ref->instr()->block(), block,
               "Instruction result is referenced by multiple blocks, values "
               "can only be used in the block they're declared in");
    }
  }
}
