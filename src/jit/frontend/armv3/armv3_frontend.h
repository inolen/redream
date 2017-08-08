#ifndef ARMV3_FRONTEND_H
#define ARMV3_FRONTEND_H

#include "jit/jit_frontend.h"

struct jit_guest;

enum armv3_block_flags {
  PC_SET = 0x1,
};

struct jit_frontend *armv3_frontend_create(struct jit_guest *guest);

#endif
