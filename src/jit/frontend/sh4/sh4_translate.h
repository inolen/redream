#ifndef SH4_BUILDER_H
#define SH4_BUILDER_H

#include <stdint.h>

struct ir;
struct jit_guest;

void sh4_translate(const struct jit_guest *guest, uint32_t addr, int size,
                   int flags, struct ir *ir);

#endif
