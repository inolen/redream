#ifndef ARMV3_FALLBACK_H
#define ARMV3_FALLBACK_H

#include "jit/frontend/armv3/armv3_disasm.h"

struct armv3_guest;

typedef void (*armv3_fallback_cb)(struct armv3_guest *, uint32_t,
                                  union armv3_instr);

extern armv3_fallback_cb armv3_fallbacks[NUM_ARMV3_OPS];

static inline armv3_fallback_cb armv3_get_fallback(uint32_t instr) {
  return armv3_fallbacks[armv3_get_op(instr)];
}

#endif
