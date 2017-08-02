#ifndef JIT_H
#define JIT_H

#include <stdio.h>
#include "core/list.h"
#include "core/rb_tree.h"

struct address_space;
struct cfa;
struct cprop;
struct dce;
struct ir;
struct lse;
struct ra;
struct val;

enum {
  JIT_STATE_VALID,
  JIT_STATE_INVALID,
};

struct jit_edge;
struct jit_func;

struct jit_edge {
  struct jit_func *src;
  struct jit_func *dst;

  /* location of branch instruction in host memory */
  void *branch;

  /* location of target instruction in guest memory */
  uint32_t target;

  /* has this branch been patched */
  int patched;

  /* iterators for edge lists */
  struct list_node in_it;
  struct list_node out_it;
};

/* translation information entry. maps guest blocks and instructions to their
   location in memory */
struct jit_tie {
  uint8_t *block_addr;
  uint8_t *instr_addr;
};

struct jit_func {
  int state;

  uint32_t guest_addr;
  int guest_size;

  uint8_t *host_addr;
  int host_size;

  /* temporary iterator used during compilation */
  struct list_node invalid_it;

  /* edges to other funcs */
  struct list in_edges;
  struct list out_edges;

  /* lookup map iterators */
  struct rb_node it;
  struct rb_node rit;

  /* map guest to host addresses */
  struct jit_tie ties[];
};

struct jit {
  char tag[32];

  struct jit_frontend *frontend;
  struct jit_backend *backend;
  struct exception_handler *exc_handler;

  /* passes */
  struct cfa *cfa;
  struct lse *lse;
  struct cprop *cprop;
  struct esimp *esimp;
  struct dce *dce;
  struct ra *ra;

  /* scratch compilation buffer */
  uint8_t ir_buffer[1024 * 1024 * 2];

  /* compiled funcs */
  struct jit_func *curr_func;
  struct rb_tree funcs;
  struct rb_tree reverse_funcs;

  /* compiled block perf map */
  FILE *perf_map;

  /* dump ir to application directory as blocks compile */
  int dump_code;
};

struct jit *jit_create(const char *tag, struct jit_frontend *frontend,
                       struct jit_backend *backend);
void jit_destroy(struct jit *jit);

void jit_run(struct jit *jit, int cycles);

void jit_compile_code(struct jit *jit, uint32_t guest_addr);
void jit_link_code(struct jit *jit, void *code, uint32_t target);
void jit_invalidate_code(struct jit *jit);
void jit_free_code(struct jit *jit);

#endif
