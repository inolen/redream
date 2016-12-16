#ifndef JIT_BACKEND_H
#define JIT_BACKEND_H

#include <stdint.h>

struct address_space;
struct exception;
struct ir;
struct jit;

struct jit_register {
  const char *name;
  int value_types;
  const void *data;
};

struct jit_backend {
  struct jit *jit;

  const struct jit_register *registers;
  int num_registers;

  void (*reset)(struct jit_backend *base);
  void *(*assemble_code)(struct jit_backend *base, struct ir *ir, int *size);
  void (*disassemble_code)(struct jit_backend *base, const uint8_t *code,
                           int size, int dump, int *num_instrs);

  int (*handle_exception)(struct jit_backend *base, struct exception *ex);
};

#endif
