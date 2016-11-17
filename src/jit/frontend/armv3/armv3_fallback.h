#ifndef ARMV3_FALLBACK_H
#define ARMV3_FALLBACK_H

#include <stdint.h>

struct armv3_guest;

void *armv3_fallback(uint32_t instr);

#endif
