#include "cpu/backend/interpreter/interpreter_block.h"

using namespace dreavm::emu;

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

uint32_t CallBlock(RuntimeBlock *block, Memory *memory, void *guest_ctx) {
  IntBlock *int_block = reinterpret_cast<IntBlock *>(block->priv);

  static const int NUM_REGS = sizeof(int_registers) / sizeof(Register);
  IntValue registers[NUM_REGS];

  uint8_t *locals = reinterpret_cast<uint8_t *>(alloca(int_block->locals_size));
  memset(locals, 0, int_block->locals_size);

  IntInstr *instr = nullptr;
  uint32_t i = 0;
  bool done = false;

  while (!done) {
    instr = &int_block->instrs[i];
    done = i == (uint32_t)int_block->num_instrs - 1;
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

void DumpBlock(RuntimeBlock *block) { LOG_INFO("Unimplemented"); }
}
}
}
}
