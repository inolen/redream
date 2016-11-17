#ifndef ARMV3_FRONTEND_H
#define ARMV3_FRONTEND_H

#include "jit/jit.h"

struct jit_frontend;
struct armv3_context;

enum armv3_block_flags { PC_SET };

struct armv3_guest {
  struct jit_guest;

  //
  struct armv3_context *ctx;

  //
  void *self;
  void (*switch_mode)(void *, uint64_t);
  void (*restore_mode)(void *);
  void (*software_interrupt)(void *);
};

struct jit_frontend *armv3_frontend_create(const struct armv3_guest *guest);
void armv3_frontend_destroy(struct jit_frontend *frontend);

#endif
