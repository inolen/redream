#ifndef ARMV3_TRANSLATE_H
#define ARMV3_TRANSLATE_H

#include <stdint.h>

struct armv3_guest;
struct ir;

void armv3_translate(const struct armv3_guest *guest, uint32_t addr, int size,
                     int flags, struct ir *ir);

#endif
