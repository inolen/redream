#include "jit/passes/constant_propagation_pass.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_PASS_STAT(constants_folded, "const operations folded");
DEFINE_PASS_STAT(could_optimize_binary_op, "const binary operations possible");
DEFINE_PASS_STAT(could_optimize_unary_op, "const unary operations possible");

static void cprop_run_block(struct cprop *cprop, struct ir *ir,
                            struct ir_block *block) {
  list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
    struct ir_value *arg0 = instr->arg[0];
    struct ir_value *arg1 = instr->arg[1];
    struct ir_value *result = instr->result;

    /* fold constant binary ops */
    if (arg0 && ir_is_constant(arg0) && ir_is_int(arg0->type) && arg1 &&
        ir_is_constant(arg1) && ir_is_int(arg1->type) && result) {
      uint64_t lhs = ir_zext_constant(arg0);
      uint64_t rhs = ir_zext_constant(arg1);

      struct ir_value *folded = NULL;
      switch (instr->op) {
        case OP_ADD:
          folded = ir_alloc_int(ir, lhs + rhs, result->type);
          break;
        case OP_AND:
          folded = ir_alloc_int(ir, lhs & rhs, result->type);
          break;
        case OP_DIV:
          folded = ir_alloc_int(ir, lhs / rhs, result->type);
          break;
        case OP_LSHR:
          folded = ir_alloc_int(ir, lhs >> rhs, result->type);
          break;
        case OP_OR:
          folded = ir_alloc_int(ir, lhs | rhs, result->type);
          break;
        case OP_SHL:
          folded = ir_alloc_int(ir, lhs << rhs, result->type);
          break;
        case OP_SUB:
          folded = ir_alloc_int(ir, lhs - rhs, result->type);
          break;
        case OP_UMUL:
          folded = ir_alloc_int(ir, lhs * rhs, result->type);
          break;
        case OP_XOR:
          folded = ir_alloc_int(ir, lhs ^ rhs, result->type);
          break;
        default:
          STAT_could_optimize_binary_op++;
          continue;
      }

      if (folded) {
        ir_replace_uses(result, folded);
        STAT_constants_folded++;
      }
    }
    /* fold constant unary ops */
    else if (arg0 && !arg1 && ir_is_constant(arg0) && ir_is_int(arg0->type) &&
             result) {
      uint64_t arg = ir_zext_constant(arg0);

      struct ir_value *folded = NULL;
      switch (instr->op) {
        case OP_NEG:
          folded = ir_alloc_int(ir, 0 - arg, result->type);
          break;
        case OP_NOT:
          folded = ir_alloc_int(ir, ~arg, result->type);
          break;
        /* filter the load instructions out of the "could optimize" stats */
        case OP_LOAD_HOST:
        case OP_LOAD_GUEST:
        case OP_LOAD_FAST:
        case OP_LOAD_CONTEXT:
        case OP_LOAD_LOCAL:
          break;
        default:
          STAT_could_optimize_unary_op++;
          continue;
      }

      if (folded) {
        ir_replace_uses(result, folded);
        STAT_constants_folded++;
      }
    }
  }
}

void cprop_run(struct cprop *cprop, struct ir *ir) {
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    cprop_run_block(cprop, ir, block);
  }
}

void cprop_destroy(struct cprop *cprop) {}

struct cprop *cprop_create() {
  return NULL;
}
