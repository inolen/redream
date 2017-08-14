#include "jit/passes/control_flow_analysis_pass.h"
#include "core/list.h"
#include "jit/ir/ir.h"

void cfa_run(struct cfa *cfa, struct ir *ir) {
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      /* add edges between blocks for easy traversing */
      if (instr->op == OP_BRANCH || instr->op == OP_BRANCH_FALSE ||
          instr->op == OP_BRANCH_TRUE) {
        if (instr->arg[0]->blk) {
          ir_add_edge(ir, block, instr->arg[0]->blk);
        }
      }
    }
  }
}

void cfa_destroy(struct cfa *cfa) {}

struct cfa *cfa_create() {
  return NULL;
}
