#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include "jit/backend/jit_backend.h"

extern const struct jit_register x64_registers[];
extern const int x64_num_registers;

struct jit_backend *x64_backend_create(struct jit *jit, void *code,
                                       int code_size, int stack_size);
void x64_backend_destroy(struct jit_backend *b);

#endif
