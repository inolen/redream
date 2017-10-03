#include "jit/passes/conversion_elimination_pass.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_PASS_STAT(sext_removed, "sign extends eliminated");
DEFINE_PASS_STAT(zext_removed, "zero extends eliminated");
DEFINE_PASS_STAT(trunc_removed, "truncations eliminated");

static void cve_run_block(struct ir *ir, struct ir_block *block) {
  list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
    /* eliminate unnecessary sext / zext operations */
    if (instr->op == OP_LOAD_HOST || instr->op == OP_LOAD_GUEST ||
        instr->op == OP_LOAD_FAST || instr->op == OP_LOAD_CONTEXT) {
      enum ir_type memory_type = VALUE_V;
      int same_type = 1;
      int all_sext = 1;
      int all_zext = 1;

      list_for_each_entry(use, &instr->result->uses, struct ir_use, it) {
        struct ir_instr *use_instr = use->instr;
        struct ir_value *use_result = use_instr->result;

        if (use_instr->op == OP_SEXT || use_instr->op == OP_ZEXT) {
          if (memory_type == VALUE_V) {
            memory_type = use_result->type;
          }

          if (memory_type != use_result->type) {
            same_type = 0;
          }
        }

        if (use_instr->op != OP_SEXT) {
          all_sext = 0;
        }

        if (use_instr->op != OP_ZEXT) {
          all_zext = 0;
        }
      }

      if (same_type && all_sext) {
        /* TODO implement */

        STAT_sext_removed++;
      } else if (same_type && all_zext) {
        /* TODO implement */

        STAT_zext_removed++;
      }
    } else if (instr->op == OP_STORE_HOST || instr->op == OP_STORE_GUEST ||
               instr->op == OP_STORE_FAST || instr->op == OP_STORE_CONTEXT) {
      struct ir_value *store_value = instr->arg[1];

      if (store_value->def && store_value->def->op == OP_TRUNC) {
        /* TODO implement */

        /* note, don't actually remove the truncation as other values may
           reference it. let DCE clean it up */
        STAT_trunc_removed++;
      }
    }
  }
}

void cve_run(struct ir *ir) {
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    cve_run_block(ir, block);
  }
}
