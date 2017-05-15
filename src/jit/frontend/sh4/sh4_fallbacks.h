#ifndef SH4_FALLBACKS_H
#define SH4_FALLBACKS_H

#include "jit/frontend/sh4/sh4_disasm.h"

struct sh4_guest;

typedef void (*sh4_fallback_cb)(struct sh4_guest *, uint32_t, union sh4_instr);

extern sh4_fallback_cb sh4_fallbacks[NUM_SH4_OPS];

static inline sh4_fallback_cb sh4_get_fallback(uint16_t instr) {
  return sh4_fallbacks[sh4_get_op(instr)];
}

#endif
