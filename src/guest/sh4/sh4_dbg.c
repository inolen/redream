#include "guest/debugger.h"
#include "guest/memory.h"
#include "guest/sh4/sh4.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_fallback.h"

struct breakpoint {
  uint32_t addr;
  uint16_t instr;
  struct list_node it;
};

static struct breakpoint *lookup_breakpoint(struct sh4 *sh4, uint32_t addr) {
  list_for_each_entry(bp, &sh4->breakpoints, struct breakpoint, it) {
    if (bp->addr == addr) {
      return bp;
    }
  }
  return NULL;
}

static void destroy_breakpoint(struct sh4 *sh4, struct breakpoint *bp) {
  list_remove(&sh4->breakpoints, &bp->it);
  free(bp);
}

static struct breakpoint *create_breakpoint(struct sh4 *sh4, uint32_t addr,
                                            uint16_t instr) {
  struct breakpoint *bp = calloc(1, sizeof(struct breakpoint));
  bp->addr = addr;
  bp->instr = instr;
  list_add(&sh4->breakpoints, &bp->it);
  return bp;
}

int sh4_dbg_invalid_instr(struct sh4 *sh4) {
  uint32_t pc = sh4->ctx.pc;

  /* ensure a breakpoint exists for this address */
  struct breakpoint *bp = lookup_breakpoint(sh4, pc);

  if (!bp) {
    return 0;
  }

  /* force a break from dispatch */
  sh4->ctx.run_cycles = 0;

  /* let the debugger know execution has stopped */
  debugger_trap(sh4->dc->debugger);

  return 1;
}

void sh4_dbg_read_register(struct device *dev, int n, uint64_t *value,
                           int *size) {
  struct sh4 *sh4 = (struct sh4 *)dev;

  if (n < 16) {
    *value = sh4->ctx.r[n];
  } else if (n == 16) {
    *value = sh4->ctx.pc;
  } else if (n == 17) {
    *value = sh4->ctx.pr;
  } else if (n == 18) {
    *value = sh4->ctx.gbr;
  } else if (n == 19) {
    *value = sh4->ctx.vbr;
  } else if (n == 20) {
    *value = sh4->ctx.mach;
  } else if (n == 21) {
    *value = sh4->ctx.macl;
  } else if (n == 22) {
    *value = sh4->ctx.sr;
  } else if (n == 23) {
    *value = sh4->ctx.fpul;
  } else if (n == 24) {
    *value = sh4->ctx.fpscr;
  } else if (n < 41) {
    *value = sh4->ctx.fr[n - 25];
  } else if (n == 41) {
    *value = sh4->ctx.ssr;
  } else if (n == 42) {
    *value = sh4->ctx.spc;
  } else if (n < 51) {
    uint32_t *b0 = (sh4->ctx.sr & RB_MASK) ? sh4->ctx.ralt : sh4->ctx.r;
    *value = b0[n - 43];
  } else if (n < 59) {
    uint32_t *b1 = (sh4->ctx.sr & RB_MASK) ? sh4->ctx.r : sh4->ctx.ralt;
    *value = b1[n - 51];
  }

  *size = 4;
}

void sh4_dbg_read_memory(struct device *dev, uint32_t addr, uint8_t *buffer,
                         int size) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct memory *mem = sh4->dc->mem;

  while (size--) {
    *(buffer++) = sh4_read8(mem, addr++);
  }
}

void sh4_dbg_remove_breakpoint(struct device *dev, int type, uint32_t addr) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct memory *mem = sh4->dc->mem;

  struct breakpoint *bp = lookup_breakpoint(sh4, addr);
  CHECK_NOTNULL(bp);

  /* restore the original instruction */
  sh4_write16(mem, addr, bp->instr);

  /* free code cache to remove block containing the invalid instruction  */
  jit_free_code(sh4->jit);

  destroy_breakpoint(sh4, bp);
}

void sh4_dbg_add_breakpoint(struct device *dev, int type, uint32_t addr) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct memory *mem = sh4->dc->mem;

  uint16_t instr = sh4_read16(mem, addr);
  struct breakpoint *bp = create_breakpoint(sh4, addr, instr);

  /* write out an invalid instruction */
  sh4_write16(mem, addr, 0);

  /* free code cache to remove block containing the original instruction  */
  jit_free_code(sh4->jit);
}

void sh4_dbg_step(struct device *dev) {
  struct sh4 *sh4 = (struct sh4 *)dev;
  struct memory *mem = sh4->dc->mem;

  /* run the fallback handler for the current pc */
  uint16_t data = sh4_read16(mem, sh4->ctx.pc);
  struct jit_opdef *def = sh4_get_opdef(data);
  def->fallback((struct jit_guest *)sh4->guest, sh4->ctx.pc, data);

  /* let the debugger know we've stopped */
  debugger_trap(sh4->dc->debugger);
}

int sh4_dbg_num_registers(struct device *dev) {
  return 59;
}
