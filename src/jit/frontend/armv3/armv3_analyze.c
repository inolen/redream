#include "jit/frontend/armv3/armv3_analyze.h"
#include "core/assert.h"
#include "core/log.h"
#include "jit/frontend/armv3/armv3_disasm.h"
#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/frontend/armv3/armv3_guest.h"

void armv3_analyze_block(const struct armv3_guest *guest, uint32_t addr, int *flags,
                         int *size) {
  *size = 0;

  while (1) {
    uint32_t data = guest->r32(guest->space, addr);
    union armv3_instr i = {data};
    struct armv3_desc *desc = armv3_disasm(i.raw);

    /* end block on invalid instruction */
    if (desc->op == ARMV3_OP_INVALID) {
      break;
    }

    addr += 4;
    *size += 4;

    /* stop emitting when pc is changed */
    if ((desc->flags & FLAG_BRANCH) ||
        ((desc->flags & FLAG_DATA) && i.data.rd == 15) ||
        (desc->flags & FLAG_PSR) ||
        ((desc->flags & FLAG_XFR) && i.xfr.rd == 15) ||
        ((desc->flags & FLAG_BLK) && i.blk.rlist & (1 << 15)) ||
        (desc->flags & FLAG_SWI)) {
      *flags |= PC_SET;
      break;
    }
  }
}
