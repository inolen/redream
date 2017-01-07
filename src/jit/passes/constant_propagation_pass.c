#include "jit/passes/constant_propagation_pass.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_STAT(constants_folded, "constant operations folded");
DEFINE_STAT(could_optimize_binary_op, "constant binary operations possible");
DEFINE_STAT(could_optimize_unary_op, "constant unary operations possible");

static void cprop_run_block(struct cprop *cprop, struct ir *ir,
                            struct ir_block *block) {
  list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
    /* fold constant binary ops */
    if (instr->arg[0] && ir_is_constant(instr->arg[0]) && instr->arg[1] &&
        ir_is_constant(instr->arg[1]) && instr->result) {
      uint64_t lhs = ir_zext_constant(instr->arg[0]);
      uint64_t rhs = ir_zext_constant(instr->arg[1]);

      struct ir_value *folded = NULL;
      switch (instr->op) {
        case OP_ADD:
          folded = ir_alloc_int(ir, lhs + rhs, instr->result->type);
          break;
        case OP_AND:
          folded = ir_alloc_int(ir, lhs & rhs, instr->result->type);
          break;
        case OP_DIV:
          folded = ir_alloc_int(ir, lhs / rhs, instr->result->type);
          break;
        case OP_LSHR:
          folded = ir_alloc_int(ir, lhs >> rhs, instr->result->type);
          break;
        case OP_OR:
          folded = ir_alloc_int(ir, lhs | rhs, instr->result->type);
          break;
        case OP_SHL:
          folded = ir_alloc_int(ir, lhs << rhs, instr->result->type);
          break;
        case OP_SUB:
          folded = ir_alloc_int(ir, lhs - rhs, instr->result->type);
          break;
        case OP_UMUL:
          folded = ir_alloc_int(ir, lhs * rhs, instr->result->type);
          break;
        case OP_XOR:
          folded = ir_alloc_int(ir, lhs ^ rhs, instr->result->type);
          break;
        default:
          STAT_could_optimize_binary_op++;
          continue;
      }

      if (folded) {
        ir_replace_uses(instr->result, folded);
        STAT_constants_folded++;
      }
    }
    /* fold constant unary ops */
    else if (instr->arg[0] && !instr->arg[1] && ir_is_constant(instr->arg[0]) &&
             instr->result) {
      uint64_t arg = ir_zext_constant(instr->arg[0]);

      struct ir_value *folded = NULL;
      switch (instr->op) {
        case OP_NEG:
          folded = ir_alloc_int(ir, 0 - arg, instr->result->type);
          break;
        case OP_NOT:
          folded = ir_alloc_int(ir, ~arg, instr->result->type);
          break;
        /* filter the load instructions out of the "could optimize" stats */
        case OP_LOAD:
        case OP_LOAD_FAST:
        case OP_LOAD_SLOW:
        case OP_LOAD_CONTEXT:
        case OP_LOAD_LOCAL:
          break;
        default:
          STAT_could_optimize_unary_op++;
          continue;
      }

      if (folded) {
        ir_replace_uses(instr->result, folded);
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
