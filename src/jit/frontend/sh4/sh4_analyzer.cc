#include "core/assert.h"
#include "core/memory.h"
#include "jit/frontend/sh4/sh4_analyzer.h"
#include "jit/frontend/sh4/sh4_disassembler.h"
#include "jit/frontend/sh4/sh4_frontend.h"

using namespace re;
using namespace re::jit::frontend::sh4;

void SH4Analyzer::AnalyzeBlock(uint32_t guest_addr, uint8_t *guest_ptr,
                               int flags, int *size) {
  *size = 0;

  while (true) {
    Instr instr;
    instr.addr = guest_addr;
    instr.opcode = load<uint16_t>(guest_ptr);

    // end block on invalid instruction
    if (!SH4Disassembler::Disasm(&instr)) {
      break;
    }

    int step = (instr.flags & OP_FLAG_DELAYED) ? 4 : 2;
    guest_addr += step;
    guest_ptr += step;
    *size += step;

    // stop emitting once a branch has been hit. in addition, if fpscr has
    // changed, stop emitting since the fpu state is invalidated. also, if
    // sr has changed, stop emitting as there are interrupts that possibly
    // need to be handled
    if (instr.flags & (OP_FLAG_BRANCH | OP_FLAG_SET_FPSCR | OP_FLAG_SET_SR)) {
      break;
    }

    // used by gdb server when stepping through instructions
    if (flags & SH4_SINGLE_INSTR) {
      break;
    }
  }
}
