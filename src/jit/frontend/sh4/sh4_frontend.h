#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

struct jit_frontend;
struct jit_guest;

enum {
  SH4_DOUBLE_PR = 0x1,
  SH4_DOUBLE_SZ = 0x2,
  SH4_SINGLE_INSTR = 0x4,
};

struct jit_frontend *sh4_frontend_create(const struct jit_guest *guest);
void sh4_frontend_destroy(struct jit_frontend *frontend);

#endif
