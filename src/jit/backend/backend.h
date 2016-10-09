#ifndef BACKEND_H
#define BACKEND_H

#include <stdbool.h>
#include <stdint.h>
#include "jit/guest.h"

struct address_space;
struct ir;
struct exception;

struct jit_register {
  const char *name;
  int value_types;
  const void *data;
};

struct jit_backend;

struct jit_backend {
  const struct jit_register *registers;
  int num_registers;

  void (*reset)(struct jit_backend *base);
  const uint8_t *(*assemble_code)(struct jit_backend *, struct ir *ir,
                                  int *size);
  void (*dump_code)(struct jit_backend *base, const uint8_t *host_addr,
                    int size);
  bool (*handle_exception)(struct jit_backend *base, struct exception *ex);
};

#endif
