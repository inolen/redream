#include "jit/frontend/sh4/sh4_analyze.h"
#include "core/assert.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/jit.h"

void sh4_analyze_block(const struct jit *jit, struct sh4_analysis *as) {
  as->size = 0;
  as->cycles = 0;

  while (1) {
    struct sh4_instr instr = {0};
    instr.addr = as->addr + as->size;
    instr.opcode = jit->r16(jit->space, instr.addr);

    /* end block on invalid instruction */
    if (!sh4_disasm(&instr)) {
      break;
    }

    as->size += 2;
    as->cycles += instr.cycles;

    if (instr.flags & SH4_FLAG_DELAYED) {
      struct sh4_instr delay_instr = {0};
      delay_instr.addr = as->addr + as->size;
      delay_instr.opcode = jit->r16(jit->space, delay_instr.addr);

      CHECK(sh4_disasm(&delay_instr));
      CHECK(!(delay_instr.flags & SH4_FLAG_DELAYED));

      as->size += 2;
      as->cycles += delay_instr.cycles;
    }

    /*
     * stop emitting once a branch has been hit. in addition, if fpscr has
     * changed, stop emitting since the fpu state is invalidated. also, if
     * sr has changed, stop emitting as there are interrupts that possibly
     * need to be handled
     */
    if (instr.flags &
        (SH4_FLAG_BRANCH | SH4_FLAG_SET_FPSCR | SH4_FLAG_SET_SR)) {
      break;
    }

    /* used by debugger when stepping through instructions */
    if (as->flags & SH4_SINGLE_INSTR) {
      break;
    }
  }
}
