#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

#include <stdint.h>

struct jit;
struct sh4_frontend;

enum {
  SH4_FASTMEM = 0x1,
  SH4_DOUBLE_PR = 0x2,
  SH4_DOUBLE_SZ = 0x4,
};

extern uint32_t sh4_fsca_table[];

struct sh4_frontend *sh4_frontend_create(struct jit *jit);
void sh4_frontend_destroy(struct sh4_frontend *frontend);

#endif
