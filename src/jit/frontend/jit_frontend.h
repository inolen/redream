#ifndef JIT_FRONTEND_H
#define JIT_FRONTEND_H

#include <stdint.h>

struct ir;
struct jit;
struct jit_block;
struct jit_guest;
struct jit_frontend;

typedef void (*jit_fallback)(struct jit_guest *, uint32_t, uint32_t);

struct jit_opdef {
  int op;
  const char *desc;
  const char *sig;
  int cycles;
  int flags;
  jit_fallback fallback;
};

struct jit_frontend {
  struct jit *jit;

  void (*init)(struct jit_frontend *);
  void (*destroy)(struct jit_frontend *);

  void (*analyze_code)(struct jit_frontend *, struct jit_block *);
  void (*translate_code)(struct jit_frontend *, struct jit_block *,
                         struct ir *);
  void (*dump_code)(struct jit_frontend *, const struct jit_block *);

  const struct jit_opdef *(*lookup_op)(struct jit_frontend *, const void *);
};

#endif
