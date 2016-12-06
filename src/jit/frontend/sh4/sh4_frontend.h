#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

#include "jit/frontend/jit_frontend.h"

struct ir;
struct jit;

enum {
  SH4_FASTMEM = 0x1,
  SH4_DOUBLE_PR = 0x2,
  SH4_DOUBLE_SZ = 0x4,
  SH4_SINGLE_INSTR = 0x8,
};

struct sh4_frontend {
  struct jit_frontend;

  /* runtime interface */
  void *data;
  void (*translate)(void *, uint32_t, struct ir *, int);
  void (*invalid_instr)(void *, uint64_t);
  void (*prefetch)(void *, uint64_t);
  void (*sr_updated)(void *, uint64_t);
  void (*fpscr_updated)(void *, uint64_t);
};

struct jit_frontend *sh4_frontend_create(struct jit *jit);
void sh4_frontend_destroy(struct jit_frontend *frontend);

#endif
