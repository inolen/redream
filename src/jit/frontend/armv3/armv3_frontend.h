#ifndef ARMV3_FRONTEND_H
#define ARMV3_FRONTEND_H

#include "jit/frontend/jit_frontend.h"

enum armv3_block_flags {
  PC_SET = 0x1,
};

struct jit_frontend *armv3_frontend_create();

#endif
