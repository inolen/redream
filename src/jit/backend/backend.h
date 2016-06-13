#ifndef BACKEND_H
#define BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct address_space_s;
struct ir_s;
struct re_exception_s;

typedef struct register_def_s {
  const char *name;
  int value_types;
  const void *data;
} register_def_t;

typedef struct mem_interface_s {
  void *ctx_base;
  void *mem_base;
  struct address_space_s *mem_self;
  uint8_t (*r8)(struct address_space_s *, uint32_t);
  uint16_t (*r16)(struct address_space_s *, uint32_t);
  uint32_t (*r32)(struct address_space_s *, uint32_t);
  uint64_t (*r64)(struct address_space_s *, uint32_t);
  void (*w8)(struct address_space_s *, uint32_t, uint8_t);
  void (*w16)(struct address_space_s *, uint32_t, uint16_t);
  void (*w32)(struct address_space_s *, uint32_t, uint32_t);
  void (*w64)(struct address_space_s *, uint32_t, uint64_t);
} mem_interface_t;

struct jit_backend_s;

typedef const register_def_t *(*registers_cb)();
typedef int (*num_registers_cb)();
typedef void (*reset_cb)(struct jit_backend_s *);
typedef const uint8_t *(*assemble_code_cb)(struct jit_backend_s *,
                                           struct ir_s *, int *);
typedef void (*dump_code_cb)(struct jit_backend_s *, const uint8_t *, int);
typedef bool (*handle_exception_cb)(struct jit_backend_s *,
                                    struct re_exception_s *);

typedef struct jit_backend_s {
  const register_def_t *registers;
  int num_registers;

  reset_cb reset;
  assemble_code_cb assemble_code;
  dump_code_cb dump_code;
  handle_exception_cb handle_exception;
} jit_backend_t;

#ifdef __cplusplus
}
#endif

#endif
