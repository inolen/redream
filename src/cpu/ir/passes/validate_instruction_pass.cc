#include "core/core.h"
#include "cpu/ir/passes/validate_instruction_pass.h"

using namespace dreavm;
using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;

void ValidateInstructionPass::Run(IRBuilder &builder) {
  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      ValidateInstr(instr);
    }
  }
}

void ValidateInstructionPass::ValidateInstr(Instr *instr) {
  // after constant propagation, there shouldn't be more than a single constant
  // argument for most instructions
  Opcode op = instr->op();
  if (op != OP_STORE_CONTEXT && op != OP_BRANCH_COND && op != OP_SELECT) {
    int num_constants = 0;
    if (instr->arg0() && instr->arg0()->constant()) {
      num_constants++;
    }
    if (instr->arg1() && instr->arg1()->constant()) {
      num_constants++;
    }
    if (instr->arg2() && instr->arg2()->constant()) {
      num_constants++;
    }
    if (num_constants > 1) {
      LOG(FATAL) << "More than one constant argument detected for "
                 << Opnames[op] << " instruction";
    }
  }

  // result (reg or local) should be equal to one of the incoming arguments
}
