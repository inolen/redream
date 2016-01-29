#include "emu/profiler.h"
#include "jit/ir/passes/validate_pass.h"

using namespace dvm::jit::ir;
using namespace dvm::jit::ir::passes;

void ValidatePass::Run(IRBuilder &builder) {
  PROFILER_RUNTIME("ValidatePass::Run");

  for (auto block : builder.blocks()) {
    ValidateBlock(builder, block);
  }
}

void ValidatePass::ValidateBlock(IRBuilder &builder, Block *block) {
  Instr *tail = block->instrs().tail();

  CHECK(tail && (tail->op() == OP_BRANCH || tail->op() == OP_BRANCH_COND),
        "Block ends in a non-branch instruction");

  for (auto instr : block->instrs()) {
    CHECK((instr->op() != OP_BRANCH && instr->op() != OP_BRANCH_COND) ||
              instr == tail,
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
