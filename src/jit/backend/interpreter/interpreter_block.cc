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
  IntValue *registers = reinterpret_cast<IntValue *>(
      alloca(int_num_registers * sizeof(IntValue)));
  uint8_t *locals = reinterpret_cast<uint8_t *>(alloca(self->locals_size_));

  IntInstr *instr = self->instrs_;
  IntInstr *end = self->instrs_ + self->num_instrs_;

  while (instr < end) {
    instr->fn(instr, registers, locals);
    instr++;
  }

  return int_nextpc;
}
