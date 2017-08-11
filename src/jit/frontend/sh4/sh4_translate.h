#ifndef SH4_TRANSLATE_H
#define SH4_TRANSLATE_H

#include "jit/frontend/sh4/sh4_disasm.h"

struct ir;
struct ir_insert_point;
struct sh4_guest;

typedef void (*sh4_translate_cb)(struct sh4_guest *, struct ir *, uint32_t,
                                 union sh4_instr, int,
                                 struct ir_insert_point *);

sh4_translate_cb sh4_get_translator(uint16_t instr);

#endif
