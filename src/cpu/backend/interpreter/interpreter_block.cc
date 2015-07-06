#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/backend/interpreter/interpreter_block.h"
#include "emu/profiler.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::interpreter;
using namespace dreavm::cpu::ir;

InterpreterBlock::InterpreterBlock(int guest_cycles, IntInstr *instrs,
                                   int num_instrs, int locals_size)
    : RuntimeBlock(guest_cycles),
      instrs_(instrs),
      num_instrs_(num_instrs),
      locals_size_(locals_size) {}

InterpreterBlock::~InterpreterBlock() { free(instrs_); }

uint32_t InterpreterBlock::Call(emu::Memory *memory, void *guest_ctx) {
  IntValue registers[NUM_INT_REGS];
  uint8_t *locals = reinterpret_cast<uint8_t *>(alloca(locals_size_));
  memset(locals, 0, locals_size_);

  IntInstr *instr = nullptr;
  uint32_t i = 0;
  bool done = false;

  while (!done) {
    instr = &instrs_[i];
    done = i == (uint32_t)num_instrs_ - 1;
    // there are a few possible return values from the callbacks:
    // 1. branch isn't a branch, next instruction index is returned
    // 1. branch is a local branch, next instruction index is returned
    // 2. branch is a far, indirect branch, absolute i32 address is returned
    // 3. branch is a far, direct branch, absolute i32 address is returned
    // cases 2 or 3 will always occur as the last instruction of the block,
    // so the return can be assumed to be an i32 address then
    i = instr->fn(instr, i, memory, registers, locals, guest_ctx);
  }

  return i;
}
