#ifndef ARMV3_FRONTEND_H
#define ARMV3_FRONTEND_H

#include "jit/frontend/frontend.h"
#include "jit/frontend/frontend.h"
#include "jit/jit.h"

enum armv3_block_flags { PC_SET = 0x1 };

struct armv3_frontend {
  struct jit_frontend;

  /* runtime interface */
  void *data;
  void (*translate)(void *, uint32_t, struct ir *, int);
  void (*switch_mode)(void *, uint64_t);
  void (*restore_mode)(void *);
  void (*software_interrupt)(void *);
};

struct jit_frontend *armv3_frontend_create(struct jit *jit);
void armv3_frontend_destroy(struct jit_frontend *frontend);

#endif
