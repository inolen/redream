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
  if (instr->op() != OP_STORE_CONTEXT && instr->op() != OP_BRANCH_COND &&
      instr->arg0() && instr->arg0()->constant() && instr->arg1() &&
      instr->arg1()->constant()) {
    LOG(FATAL) << "More than one constant argument detected for "
               << Opnames[instr->op()] << " instruction";
  }

  // result (reg or local) should be equal to one of the incoming arguments
}
