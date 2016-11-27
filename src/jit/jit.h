#ifndef JIT_H
#define JIT_H

#include <stdio.h>
#include "core/rb_tree.h"

struct address_space;

typedef void (*code_pointer_t)();

struct jit_block {
  const uint8_t *host_addr;
  int host_size;
  uint32_t guest_addr;
  struct rb_node it;
  struct rb_node rit;
};

struct jit {
  char tag[32];
  struct jit_frontend *frontend;
  struct jit_backend *backend;
  struct exception_handler *exc_handler;

  /* scratch compilation buffer */
  uint8_t ir_buffer[1024 * 1024];

  /* compiled block perf map */
  FILE *perf_map;

  code_pointer_t default_code;
  code_pointer_t *code;
  struct rb_tree blocks;
  struct rb_tree reverse_blocks;

  /* dispatch interface */
  uint32_t block_mask;
  uint32_t block_shift;
  int block_max;

  /* memory interface */
  void *ctx;
  void *mem;
  struct address_space *space;
  uint8_t (*r8)(struct address_space *, uint32_t);
  uint16_t (*r16)(struct address_space *, uint32_t);
  uint32_t (*r32)(struct address_space *, uint32_t);
  uint64_t (*r64)(struct address_space *, uint32_t);
  void (*w8)(struct address_space *, uint32_t, uint8_t);
  void (*w16)(struct address_space *, uint32_t, uint16_t);
  void (*w32)(struct address_space *, uint32_t, uint32_t);
  void (*w64)(struct address_space *, uint32_t, uint64_t);
};

struct jit *jit_create(const char *code);
void jit_destroy(struct jit *jit);

int jit_init(struct jit *jit, struct jit_frontend *frontend,
             struct jit_backend *backend, code_pointer_t default_code);

struct jit_block *jit_get_block(struct jit *cache, uint32_t guest_addr);
void jit_remove_blocks(struct jit *jit, uint32_t guest_addr);
void jit_unlink_blocks(struct jit *jit);
void jit_clear_blocks(struct jit *jit);

code_pointer_t jit_compile_code(struct jit *jit, uint32_t guest_addr);

#endif
