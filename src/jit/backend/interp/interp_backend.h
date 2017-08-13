#ifndef INTERP_BACKEND_H
#define INTERP_BACKEND_H

#include "jit/jit_backend.h"

struct jit_frontend;

struct jit_backend *interp_backend_create(struct jit_guest *guest,
                                          struct jit_frontend *frontend);

#endif
