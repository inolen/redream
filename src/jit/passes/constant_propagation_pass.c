#include "jit/passes/constant_propagation_pass.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"


DEFINE_STAT(constant_propagations_removed, "constant propagations removed");
DEFINE_STAT(could_optimize_binary_op, "constant binary operations possible");
DEFINE_STAT(could_optimize_unary_op, "constant unary operations possible");

void cprop_run(struct ir *ir) {
  list_for_each_entry(instr, &ir->instrs, struct ir_instr, it) {
    
    /* Simplify binary ops with constant arguments */
    if(instr->arg[0] && ir_is_constant(instr->arg[0]) &&
       instr->arg[1] && ir_is_constant(instr->arg[1]) &&
       instr->result)
    {
      uint64_t lhs = ir_zext_constant(instr->arg[0]);
      uint64_t rhs = ir_zext_constant(instr->arg[1]);
      struct ir_value *result;
      switch(instr->op)
      {
        case OP_ADD:
          result = ir_alloc_int(ir, lhs + rhs, instr->result->type);
          break;
        case OP_AND:
          result = ir_alloc_int(ir, lhs & rhs, instr->result->type);
          break;
        case OP_DIV:
          result = ir_alloc_int(ir, lhs / rhs, instr->result->type);
          break;
        case OP_LSHR:
          result = ir_alloc_int(ir, lhs >> rhs, instr->result->type);
          break;
        case OP_OR:
          result = ir_alloc_int(ir, lhs | rhs, instr->result->type);
          break;
        case OP_SHL:
          result = ir_alloc_int(ir, lhs << rhs, instr->result->type);
          break;
        case OP_SUB:
          result = ir_alloc_int(ir, lhs - rhs, instr->result->type);
          break;
        case OP_UMUL:
          result = ir_alloc_int(ir, lhs * rhs, instr->result->type);
          break;
        case OP_XOR:
          result = ir_alloc_int(ir, lhs ^ rhs, instr->result->type);
          break;
        default:
          STAT_could_optimize_binary_op++;
          continue;
      }
      ir_replace_uses(instr->result, result);
      STAT_constant_propagations_removed++;
    }
    /* Simplify constant unary ops */
    else if(instr->arg[0] && !instr->arg[1] && ir_is_constant(instr->arg[0]) &&
            instr->result) {
      uint64_t arg = ir_zext_constant(instr->arg[0]);
      struct ir_value *result;
      switch(instr->op)
      {
        case OP_NEG:
          result = ir_alloc_int(ir, 0 - arg, instr->result->type);
          break;
        case OP_NOT:
          result = ir_alloc_int(ir, ~arg, instr->result->type);
          break;
        default:
          STAT_could_optimize_unary_op++;
          continue;
      }
      ir_replace_uses(instr->result, result);
      STAT_constant_propagations_removed++;
    }
  }
}



