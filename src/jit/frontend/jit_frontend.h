#ifndef JIT_FRONTEND_H
#define JIT_FRONTEND_H

#include <stdint.h>

struct ir;
struct jit;
struct jit_frontend;

struct jit_frontend {
  struct jit *jit;
  void (*translate_code)(struct jit_frontend *base, uint32_t addr,
                         struct ir *ir, int fastmem, int *size);
  void (*disassemble_code)(struct jit_frontend *base, uint32_t addr, int size);
};

#endif
