#include "jit/ir/passes/conversion_elimination_pass.h"
#include "jit/ir/ir.h"
#include "jit/ir/passes/pass_stat.h"

DEFINE_STAT(num_sext_removed, "Number of sext eliminated");
DEFINE_STAT(num_zext_removed, "Number of zext eliminated");
DEFINE_STAT(num_trunc_removed, "Number of trunc eliminated");

const char *cve_name = "cve";

void cve_run(struct ir *ir) {
  list_for_each_entry_safe(instr, &ir->instrs, struct ir_instr, it) {
    // eliminate unnecessary sext / zext operations
    if (instr->op == OP_LOAD_HOST || instr->op == OP_LOAD_FAST ||
        instr->op == OP_LOAD_SLOW || instr->op == OP_LOAD_CONTEXT) {
      enum ir_type memory_type = VALUE_V;
      bool same_type = true;
      bool all_sext = true;
      bool all_zext = true;

      list_for_each_entry(use, &instr->result->uses, struct ir_use, it) {
        struct ir_instr *use_instr = use->instr;
        struct ir_value *use_result = use_instr->result;

        if (use_instr->op == OP_SEXT || use_instr->op == OP_ZEXT) {
          if (memory_type == VALUE_V) {
            memory_type = use_result->type;
          }

          if (memory_type != use_result->type) {
            same_type = false;
          }
        }

        if (use_instr->op != OP_SEXT) {
          all_sext = false;
        }

        if (use_instr->op != OP_ZEXT) {
          all_zext = false;
        }
      }

      if (same_type && all_sext) {
        // TODO implement

        STAT_num_sext_removed++;
      } else if (same_type && all_zext) {
        // TODO implement

        STAT_num_zext_removed++;
      }
    } else if (instr->op == OP_STORE_HOST || instr->op == OP_STORE_FAST ||
               instr->op == OP_STORE_SLOW || instr->op == OP_STORE_CONTEXT) {
      struct ir_value *store_value = instr->arg[1];

      if (store_value->def && store_value->def->op == OP_TRUNC) {
        // TODO implement

        // note, don't actually remove the truncation as other values may
        // reference it. let DCE clean it up
        STAT_num_trunc_removed++;
      }
    }
  }
}
