#ifndef ARMV3_FRONTEND_H
#define ARMV3_FRONTEND_H

#include "jit/frontend/jit_frontend.h"

struct jit;

enum armv3_block_flags {
  PC_SET = 0x1,
};

struct armv3_frontend;

struct armv3_frontend *armv3_frontend_create(struct jit *jit);
void armv3_frontend_destroy(struct armv3_frontend *frontend);

#endif
