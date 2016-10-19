#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include "jit/backend/backend.h"

struct jit_guest;

extern const struct jit_register x64_registers[];
extern const int x64_num_registers;

struct jit_backend *x64_backend_create(const struct jit_guest *guest);
void x64_backend_destroy(struct jit_backend *b);

#endif
