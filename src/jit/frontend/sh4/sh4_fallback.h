#ifndef SH4_FALLBACK_H
#define SH4_FALLBACK_H

#include "jit/frontend/sh4/sh4_disasm.h"

struct sh4_guest;

#define SH4_INSTR(name, desc, sig, cycles, flags)                  \
  void sh4_fallback_##name(struct sh4_guest *guest, uint32_t addr, \
                           union sh4_instr i);
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR

#endif
