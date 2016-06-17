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

struct jit_backend {
  const struct register_def *registers;
  int num_registers;

  void (*reset)(struct jit_backend *base);
  const uint8_t *(*assemble_code)(struct jit_backend *, struct ir *ir,
                                  int *size);
  void (*dump_code)(struct jit_backend *base, const uint8_t *host_addr,
                    int size);
  bool (*handle_exception)(struct jit_backend *base, struct exception *ex);
};

#endif
