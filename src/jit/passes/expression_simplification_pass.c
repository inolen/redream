#include "jit/passes/expression_simplification_pass.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_PASS_STAT(bitwise_identities_removed, "bitwise identities removed");
DEFINE_PASS_STAT(zero_properties_removed, "zero properties removed");
DEFINE_PASS_STAT(zero_identities_removed, "zero identities removed");
DEFINE_PASS_STAT(one_identities_removed, "one identities removed");

static void esimp_run_block(struct esimp *esimp, struct ir *ir,
                            struct ir_block *block) {
  list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
    /* simplify bitwise identities with identical inputs */
    if (instr->op == OP_XOR && instr->arg[0] == instr->arg[1]) {
      struct ir_value *zero = ir_alloc_int(ir, 0, instr->result->type);
      ir_replace_uses(instr->result, zero);
      STAT_bitwise_identities_removed++;
    } else if ((instr->op == OP_AND || instr->op == OP_OR) &&
               instr->arg[0] == instr->arg[1]) {
      ir_replace_uses(instr->result, instr->arg[0]);
      STAT_bitwise_identities_removed++;
    }

    /* binary ops involving constants normally have the constant
       argument as the second argument */
    if (instr->arg[1] && ir_is_constant(instr->arg[1]) &&
        ir_is_int(instr->arg[1]->type)) {
      uint64_t rhs = ir_zext_constant(instr->arg[1]);
      struct ir_value *lhs = instr->arg[0];

      /* simplify binary ops where an argument of 0 always results in 0 */
      if ((instr->op == OP_AND || instr->op == OP_SMUL ||
           instr->op == OP_UMUL) &&
          rhs == 0) {
        struct ir_value *zero = ir_alloc_int(ir, 0, instr->result->type);
        ir_replace_uses(instr->result, zero);
        STAT_zero_properties_removed++;
      }

      /* simplify binary ops where 0 is an identity */
      else if ((instr->op == OP_ADD || instr->op == OP_SUB ||
                instr->op == OP_OR || instr->op == OP_XOR ||
                instr->op == OP_SHL || instr->op == OP_LSHR ||
                instr->op == OP_ASHR) &&
               rhs == 0) {
        ir_replace_uses(instr->result, lhs);
        STAT_zero_identities_removed++;
      }

      /* simplify binary ops where 1 is an identity */
      else if ((instr->op == OP_UMUL || instr->op == OP_SMUL ||
                instr->op == OP_DIV) &&
               rhs == 1) {
        ir_replace_uses(instr->result, lhs);
        STAT_one_identities_removed++;
      }
    }
  }
}

void esimp_run(struct esimp *esimp, struct ir *ir) {
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    esimp_run_block(esimp, ir, block);
  }
}

void esimp_destroy(struct esimp *esimp) {}

struct esimp *esimp_create() {
  return NULL;
}
