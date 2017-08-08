#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include "jit/jit_backend.h"

struct jit_guest;

struct jit_backend *x64_backend_create(struct jit_guest *guest, void *code,
                                       int code_size);

#endif
