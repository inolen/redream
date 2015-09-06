#include "core/core.h"
#include "cpu/ir/passes/control_flow_analysis_pass.h"
#include "emu/profiler.h"

using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;

void ControlFlowAnalysisPass::Run(IRBuilder &builder) {
  PROFILER_RUNTIME("ControlFlowAnalysisPass::Run");

  // add directed edges between blocks
  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      if (instr->op() == OP_BRANCH) {
        if (instr->arg0()->type() == VALUE_BLOCK) {
          builder.AddEdge(block, instr->arg0()->value<Block *>());
        }
      } else if (instr->op() == OP_BRANCH_COND) {
        if (instr->arg1()->type() == VALUE_BLOCK) {
          builder.AddEdge(block, instr->arg1()->value<Block *>());
        }
        if (instr->arg2()->type() == VALUE_BLOCK) {
          builder.AddEdge(block, instr->arg2()->value<Block *>());
        }
      }
    }
  }
}
