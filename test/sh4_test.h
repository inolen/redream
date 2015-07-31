#ifndef SH4_TEST_H
#define SH4_TEST_H

#include <map>

enum {
#define SH4CTX(name, member, type) SH4CTX_##name,
#include "sh4_ctx.inc"
#undef SH4CTX
  NUM_SH4CTX_REGS
};

struct SH4CTXReg {
  const char *name;
  size_t offset;
  size_t size;
};

extern SH4CTXReg sh4ctx_reg[NUM_SH4CTX_REGS];

struct SH4Test {
  uint8_t *buffer;
  size_t buffer_size;
  std::map<int, uint64_t> r_in;
  std::map<int, uint64_t> r_out;
};

#endif
