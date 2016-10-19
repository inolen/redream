#ifndef JIT_FRONTEND_H
#define JIT_FRONTEND_H

#include <stdint.h>

struct ir;
struct jit_frontend;

struct jit_frontend {
  void (*translate_code)(struct jit_frontend *base, uint32_t addr, int flags,
                         int *size, struct ir *ir);
  void (*dump_code)(struct jit_frontend *base, uint32_t addr, int size);
};

#endif
