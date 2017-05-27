#ifndef JIT_FRONTEND_H
#define JIT_FRONTEND_H

#include <stdint.h>

struct ir;
struct jit;
struct jit_block;
struct jit_frontend;

struct jit_opdef {
  int op;
  const char *desc;
  const char *sig;
  int cycles;
  int flags;
};

struct jit_frontend {
  struct jit *jit;

  void (*init)(struct jit_frontend *base);
  void (*translate_code)(struct jit_frontend *base, struct jit_block *block,
                         struct ir *ir);
  void (*dump_code)(struct jit_frontend *base, uint32_t addr, int size);
};

#endif
