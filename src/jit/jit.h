#ifndef JIT_H
#define JIT_H

#include <stdio.h>
#include "core/rb_tree.h"

struct address_space;

typedef void (*code_pointer_t)();

enum {
  JIT_SLOWMEM = 0x80000000,
};

struct jit_block {
  const uint8_t *host_addr;
  int host_size;
  uint32_t guest_addr;
  int guest_size;
  int flags;
  struct rb_node it;
  struct rb_node rit;
};

struct jit_guest {
  /* used by the jit to map guest addresses to block offsets */
  uint32_t block_mask;
  uint32_t block_shift;
  int block_max;

  /* used by the backend */
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

struct jit {
  struct jit_guest guest;
  struct jit_frontend *frontend;
  struct jit_backend *backend;
  struct exception_handler *exc_handler;

  code_pointer_t default_code;
  code_pointer_t *code;
  struct rb_tree blocks;
  struct rb_tree reverse_blocks;

  uint8_t ir_buffer[1024 * 1024];

  char perf_tag[32];
  FILE *perf_map;
};

struct jit *jit_create(struct jit_guest *guest, struct jit_frontend *frontend,
                       struct jit_backend *backend, code_pointer_t default_code,
                       const char *tag);
void jit_destroy(struct jit *jit);

struct jit_block *jit_get_block(struct jit *cache, uint32_t guest_addr);
void jit_remove_blocks(struct jit *jit, uint32_t guest_addr);
void jit_unlink_blocks(struct jit *jit);
void jit_clear_blocks(struct jit *jit);

code_pointer_t jit_compile_code(struct jit *jit, uint32_t guest_addr,
                                int flags);

#endif
