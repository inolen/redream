#ifndef SH4_ANALYZE_H
#define SH4_ANALYZE_H

#include <stddef.h>
#include <stdint.h>

struct jit;

struct sh4_analysis {
  uint32_t addr;
  int flags;
  int size;
  int cycles;
};

void sh4_analyze_block(const struct jit *jit, struct sh4_analysis *as);

#endif
