#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include "jit/backend/jit_backend.h"

struct jit_backend *x64_backend_create(void *code, int code_size);

#endif
