#ifndef ARMV3_FALLBACK_H
#define ARMV3_FALLBACK_H

#include "jit/frontend/armv3/armv3_disasm.h"

struct armv3_guest;

#define ARMV3_INSTR(name, desc, sig, cycles, flags)                    \
  void armv3_fallback_##name(struct armv3_guest *guest, uint32_t addr, \
                             union armv3_instr i);
#include "jit/frontend/armv3/armv3_instr.inc"
#undef ARMV3_INSTR

#endif
