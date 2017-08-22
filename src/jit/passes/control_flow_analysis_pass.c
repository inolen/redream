#include "jit/passes/control_flow_analysis_pass.h"
#include "core/list.h"
#include "jit/ir/ir.h"

void cfa_run(struct cfa *cfa, struct ir *ir) {
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    struct ir_instr *last_instr =
        list_last_entry(&block->instrs, struct ir_instr, it);

    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      if (instr == last_instr) {
        continue;
      }

      CHECK(instr->op != OP_BRANCH && instr->op != OP_BRANCH_COND,
            "only the last instruction in the block can branch");
    }

    /* add edges between blocks for easy traversing */
    if (last_instr->op == OP_BRANCH) {
      if (last_instr->arg[0]->type == VALUE_BLOCK) {
        ir_add_edge(ir, block, last_instr->arg[0]->blk);
      }
    } else if (last_instr->op == OP_BRANCH_COND) {
      if (last_instr->arg[0]->type == VALUE_BLOCK) {
        ir_add_edge(ir, block, last_instr->arg[0]->blk);
      }
      if (last_instr->arg[1]->type == VALUE_BLOCK) {
        ir_add_edge(ir, block, last_instr->arg[1]->blk);
      }
    }
  }
}

void cfa_destroy(struct cfa *cfa) {}

struct cfa *cfa_create() {
  return NULL;
}
