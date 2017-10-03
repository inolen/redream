#include "jit/passes/dead_code_elimination_pass.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_PASS_STAT(dead_removed, "dead instructions eliminated");

static void dce_run_block(struct dce *dce, struct ir *ir,
                          struct ir_block *block) {
  /* iterate in reverse in order to remove groups of dead instructions that
     only use eachother */
  list_for_each_entry_safe_reverse(instr, &block->instrs, struct ir_instr, it) {
    struct ir_value *result = instr->result;

    if (!result) {
      continue;
    }

    if (list_empty(&result->uses)) {
      ir_remove_instr(ir, instr);

      STAT_dead_removed++;
    }
  }
}

void dce_run(struct dce *dce, struct ir *ir) {
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    dce_run_block(dce, ir, block);
  }
}

void dce_destroy(struct dce *dce) {}

struct dce *dce_create() {
  return NULL;
}
