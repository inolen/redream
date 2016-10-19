#ifndef SH4_ANALYZE_H
#define SH4_ANALYZE_H

#include <stddef.h>
#include <stdint.h>

struct jit_guest;

void sh4_analyze_block(const struct jit_guest *guest, uint32_t addr, int flags,
                       int *size);

#endif
