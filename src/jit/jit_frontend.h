#ifndef JIT_FRONTEND_H
#define JIT_FRONTEND_H

#include <stdint.h>
#include <stdio.h>

struct ir;
struct jit_block;
struct jit_guest;
struct jit_frontend;

typedef void (*jit_fallback)(struct jit_guest *, uint32_t, uint32_t);

struct jit_opdef {
  int op;
  const char *name;
  const char *desc;
  const char *sig;
  int cycles;
  int flags;
  jit_fallback fallback;
};

struct jit_frontend {
  struct jit_guest *guest;

  void (*destroy)(struct jit_frontend *);

  void (*analyze_code)(struct jit_frontend *, uint32_t, int *);
  void (*translate_code)(struct jit_frontend *, uint32_t, int, struct ir *);
  void (*dump_code)(struct jit_frontend *, uint32_t, int, FILE *output);

  const struct jit_opdef *(*lookup_op)(struct jit_frontend *, const void *);
};

#endif
