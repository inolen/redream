#include "emu/profiler.h"
#include "jit/ir/passes/dead_code_elimination_pass.h"

using namespace re::jit::backend;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;

void DeadCodeEliminationPass::Run(IRBuilder &builder, bool debug) {
  PROFILER_RUNTIME("DeadCodeEliminationPass::Run");

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
    }
  }
}
