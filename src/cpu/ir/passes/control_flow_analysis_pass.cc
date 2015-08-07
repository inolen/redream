#include "core/core.h"
#include "cpu/ir/passes/control_flow_analysis_pass.h"
#include "emu/profiler.h"

using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;

void ControlFlowAnalysisPass::Run(IRBuilder &builder) {
  PROFILER_SCOPE("runtime", "ControlFlowAnalysisPass::Run");

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

  // do a postorder depth-first search of blocks starting at the head,
  // generating a reverse postorder linked list in the process
  Block *next = nullptr;
  Block *tail = nullptr;

  std::function<void(Block *)> DFS = [&](Block *block) {
    // avoid cycles. tail block has a null next, so check for it explicitly
    if (block == tail || block->rpo_next()) {
      return;
    }

    for (auto edge : block->outgoing()) {
      DFS(edge->dst());
    }

    CHECK_NE(block, next);
    block->set_rpo_next(next);
    next = block;

    if (!tail) {
      tail = block;
    }
  };

  DFS(builder.blocks().head());
}
