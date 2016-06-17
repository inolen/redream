#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/pass_stat.h"
#include "jit/ir/ir.h"

DEFINE_STAT(num_dead_removed, "Number of dead instructions eliminated");

void dce_run(struct ir *ir) {
  // iterate in reverse in order to remove groups of dead instructions that
  // only use eachother
  list_for_each_entry_safe_reverse(instr, &ir->instrs, struct ir_instr, it) {
    struct ir_value *result = instr->result;

    if (!result) {
      continue;
    }

    if (list_empty(&result->uses)) {
      ir_remove_instr(ir, instr);

      STAT_num_dead_removed++;
    }
  }
}
