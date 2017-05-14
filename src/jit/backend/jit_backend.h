#ifndef JIT_BACKEND_H
#define JIT_BACKEND_H

#include <stdint.h>

struct exception;
struct ir;
struct jit;
struct jit_block;

struct jit_register {
  const char *name;
  int value_types;
  const void *data;
};

struct jit_backend {
  struct jit *jit;

  const struct jit_register *registers;
  int num_registers;

  void (*init)(struct jit_backend *base);

  /* compile interface */
  void (*reset)(struct jit_backend *base);
  int (*assemble_code)(struct jit_backend *base, struct jit_block *block,
                       struct ir *ir);
  void (*dump_code)(struct jit_backend *base, const uint8_t *code, int size);
  int (*handle_exception)(struct jit_backend *base, struct exception *ex);

  /* dispatch interface */
  void (*run_code)(struct jit_backend *base, int cycles);
  void *(*lookup_code)(struct jit_backend *base, uint32_t);
  void (*cache_code)(struct jit_backend *base, uint32_t, void *);
  void (*invalidate_code)(struct jit_backend *base, uint32_t);
  void (*patch_edge)(struct jit_backend *base, void *, void *);
  void (*restore_edge)(struct jit_backend *base, void *, uint32_t);
};

#endif
