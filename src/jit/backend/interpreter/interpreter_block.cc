#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/interpreter/interpreter_block.h"

using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend::interpreter;

InterpreterBlock::InterpreterBlock(int guest_cycles, IntInstr *instrs,
                                   int num_instrs, int locals_size)
    : RuntimeBlock(&InterpreterBlock::Call, guest_cycles),
      instrs_(instrs),
      num_instrs_(num_instrs),
      locals_size_(locals_size) {}

uint32_t InterpreterBlock::Call(RuntimeBlock *block) {
  InterpreterBlock *self = reinterpret_cast<InterpreterBlock *>(block);

  IntInstr *instr = self->instrs_;
  int n = self->num_instrs_;

  int_state.sp += self->locals_size_;
  CHECK_LT(int_state.sp, MAX_INT_STACK);

  for (int i = 0; i < n; i++) {
    instr[i].fn(&instr[i]);
  }

  int_state.sp -= self->locals_size_;

  return int_state.pc;
}
