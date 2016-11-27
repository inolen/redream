#ifndef ARMV3_ANALYZE_H
#define ARMV3_ANALYZE_H

#include <stdint.h>

struct jit;

void armv3_analyze_block(const struct jit *jit, uint32_t addr, int *flags,
                         int *size);

#endif
