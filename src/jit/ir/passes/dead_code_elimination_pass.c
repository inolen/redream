#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/pass_stat.h"
#include "jit/ir/ir.h"

DEFINE_STAT(num_dead_removed, "Number of dead instructions eliminated");

void dce_run(ir_t *ir) {
  // iterate in reverse in order to remove groups of dead instructions that
  // only use eachother
  list_for_each_entry_safe_reverse(instr, &ir->instrs, ir_instr_t, it) {
    ir_value_t *result = instr->result;

    if (!result) {
      continue;
    }

    if (list_empty(&result->uses)) {
      ir_remove_instr(ir, instr);

      STAT_num_dead_removed++;
    }
  }
}
