#include "jit/ir/passes/dead_code_elimination_pass.h"

using namespace re::jit::backend;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;

DEFINE_STAT(num_dead_removed, "Number of dead instructions eliminated");

void DeadCodeEliminationPass::Run(IRBuilder &builder) {
  // iterate in reverse in order to remove groups of dead instructions that
  // only use eachother
  auto it = builder.instrs().rbegin();
  auto end = builder.instrs().rend();

  while (it != end) {
    Instr *instr = *(it++);

    if (instr->type() == VALUE_V) {
      continue;
    }

    if (!instr->uses().head()) {
      builder.RemoveInstr(instr);

      num_dead_removed++;
    }
  }
}
