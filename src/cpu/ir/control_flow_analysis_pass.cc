#include "core/core.h"
#include "cpu/ir/control_flow_analysis_pass.h"

using namespace dreavm;
using namespace dreavm::cpu::ir;

void ControlFlowAnalysisPass::Run(IRBuilder &builder) {
  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      Block *dst = nullptr;

      if (instr->op() == OP_BRANCH) {
        if (instr->arg0()->type() == VALUE_BLOCK) {
          dst = instr->arg0()->value<Block *>();
        }
      } else if (instr->op() == OP_BRANCH_COND) {
        if (instr->arg1()->type() == VALUE_BLOCK) {
          dst = instr->arg1()->value<Block *>();
        }
        if (instr->arg2()->type() == VALUE_BLOCK) {
          dst = instr->arg2()->value<Block *>();
        }
      }

      if (dst) {
        builder.AddEdge(block, dst);
      }
    }
  }
}
