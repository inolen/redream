#ifndef ARMV3_TRANSLATE_H
#define ARMV3_TRANSLATE_H

#include <stdint.h>

struct armv3_frontend;
struct ir;

void armv3_emit_instr(struct armv3_frontend *frontend, struct ir *ir, int flags,
                      uint32_t addr, uint32_t instr);

#endif
