#include "jit/frontend/sh4/sh4_analyze.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/jit.h"

void sh4_analyze_block(const struct jit_guest *guest, uint32_t addr, int flags,
                       int *size) {
  *size = 0;

  while (1) {
    struct sh4_instr instr = {0};
    instr.addr = addr;
    instr.opcode = guest->r16(guest->mem_self, instr.addr);

    // end block on invalid instruction
    if (!sh4_disasm(&instr)) {
      break;
    }

    int step = (instr.flags & SH4_FLAG_DELAYED) ? 4 : 2;
    addr += step;
    *size += step;

    // stop emitting once a branch has been hit. in addition, if fpscr has
    // changed, stop emitting since the fpu state is invalidated. also, if
    // sr has changed, stop emitting as there are interrupts that possibly
    // need to be handled
    if (instr.flags &
        (SH4_FLAG_BRANCH | SH4_FLAG_SET_FPSCR | SH4_FLAG_SET_SR)) {
      break;
    }

    // used by gdb server when stepping through instructions
    if (flags & SH4_SINGLE_INSTR) {
      break;
    }
  }
}
