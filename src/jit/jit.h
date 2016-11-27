#ifndef JIT_H
#define JIT_H

#include <stdio.h>
#include "core/list.h"
#include "core/rb_tree.h"

struct address_space;
struct ir;

struct jit_block {
  /* address of source block in guest memory */
  uint32_t guest_addr;

  /* address of compiled block in host memory */
  void *host_addr;
  int host_size;

  /* edges to other blocks */
  struct list in_edges;
  struct list out_edges;

  /* lookup map iterators */
  struct rb_node it;
  struct rb_node rit;
};

struct jit_edge {
  struct jit_block *src;
  struct jit_block *dst;

  /* location of branch instruction in host memory */
  void *branch;

  /* has this branch been patched */
  int patched;

  /* iterators for edge lists */
  struct list_node in_it;
  struct list_node out_it;
};

struct jit {
  char tag[32];

  /* */
  struct jit_frontend *frontend;

  /* */
  struct jit_backend *backend;

  /* backend dispatch interface */
  void *(*lookup_code)(uint32_t);
  void (*cache_code)(uint32_t, void *);
  void (*invalidate_code)(uint32_t);
  void (*patch_edge)(void *, void *);
  void (*restore_edge)(void *, uint32_t);

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

  struct exception_handler *exc_handler;

  /* scratch compilation buffer */
  uint8_t ir_buffer[1024 * 1024];

  /* compiled blocks */
  struct rb_tree blocks;
  struct rb_tree reverse_blocks;

  /* compiled block perf map */
  FILE *perf_map;
};

struct jit *jit_create(const char *tag);
void jit_destroy(struct jit *jit);

int jit_init(struct jit *jit, struct jit_frontend *frontend,
             struct jit_backend *backend);

void jit_compile_block(struct jit *jit, uint32_t guest_addr);
void jit_add_edge(struct jit *jit, void *code, uint32_t dst);

void jit_invalidate_blocks(struct jit *jit);
void jit_free_blocks(struct jit *jit);

#endif
