#ifndef SH4_ANALYZER_H
#define SH4_ANALYZER_H

#include <stddef.h>
#include <stdint.h>

enum {
  SH4_SLOWMEM = 0x1,
  SH4_DOUBLE_PR = 0x2,
  SH4_DOUBLE_SZ = 0x4,
  SH4_SINGLE_INSTR = 0x8,
};

struct jit_guest;

void sh4_analyze_block(const struct jit_guest *guest, uint32_t addr, int flags,
                       int *size);

#endif
