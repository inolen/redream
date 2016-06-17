#ifndef BACKEND_H
#define BACKEND_H

#include <stdbool.h>
#include <stdint.h>

struct address_space;
struct ir;
struct exception;

struct register_def {
  const char *name;
  int value_types;
  const void *data;
};

struct mem_interface {
  void *ctx_base;
  void *mem_base;
  struct address_space *mem_self;
  uint8_t (*r8)(struct address_space *, uint32_t);
  uint16_t (*r16)(struct address_space *, uint32_t);
  uint32_t (*r32)(struct address_space *, uint32_t);
  uint64_t (*r64)(struct address_space *, uint32_t);
  void (*w8)(struct address_space *, uint32_t, uint8_t);
  void (*w16)(struct address_space *, uint32_t, uint16_t);
  void (*w32)(struct address_space *, uint32_t, uint32_t);
  void (*w64)(struct address_space *, uint32_t, uint64_t);
};

struct jit_backend;

typedef const struct register_def *(*registers_cb)();
typedef int (*num_registers_cb)();
typedef void (*reset_cb)(struct jit_backend *);
typedef const uint8_t *(*assemble_code_cb)(struct jit_backend *, struct ir *,
                                           int *);
typedef void (*dump_code_cb)(struct jit_backend *, const uint8_t *, int);
typedef bool (*handle_exception_cb)(struct jit_backend *, struct exception *);

struct jit_backend {
  const struct register_def *registers;
  int num_registers;

  reset_cb reset;
  assemble_code_cb assemble_code;
  dump_code_cb dump_code;
  handle_exception_cb handle_exception;
};

#endif
