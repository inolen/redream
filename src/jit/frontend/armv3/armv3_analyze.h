#ifndef ARMV3_ANALYZE_H
#define ARMV3_ANALYZE_H

#include <stdint.h>

struct armv3_guest;

void armv3_analyze_block(const struct armv3_guest *guest, uint32_t addr,
                         int *flags, int *size);

#endif
