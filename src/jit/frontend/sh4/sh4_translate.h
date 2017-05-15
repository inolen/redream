#ifndef SH4_TRANSLATE_H
#define SH4_TRANSLATE_H

#include "jit/frontend/sh4/sh4_disasm.h"

struct ir;
struct sh4_guest;

typedef void (*sh4_translate_cb)(struct sh4_guest *, struct ir *, int, uint32_t,
                                 union sh4_instr);

extern sh4_translate_cb sh4_translators[NUM_SH4_OPS];

static inline sh4_translate_cb sh4_get_translator(uint16_t instr) {
  return sh4_translators[sh4_op(instr)];
}

#endif
