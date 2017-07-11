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

typedef uint32_t (*mem_read_cb)(void *, uint32_t, uint32_t);
typedef void (*mem_write_cb)(void *, uint32_t, uint32_t, uint32_t);

enum {
  BRANCH_STATIC,
  BRANCH_STATIC_TRUE,
  BRANCH_STATIC_FALSE,
  BRANCH_DYNAMIC,
  BRANCH_DYNAMIC_TRUE,
  BRANCH_DYNAMIC_FALSE,
};

struct jit_block {
  /* address of source block in guest memory */
  uint32_t guest_addr;
  int guest_size;

  /* address of compiled block in host memory */
  void *host_addr;
  int host_size;

  /* compile with fastmem support */
  int fastmem;

  /* destination address of terminating branch */
  int branch_type;
  uint32_t branch_addr;

  /* address of next instruction after branch */
  uint32_t next_addr;

  /* is block an idle loop */
  int idle_loop;

  /* number of guest instructions in block */
  int num_instrs;

  /* estimated number of guest cycles to execute block */
  int num_cycles;

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

struct jit_guest {
  /* mask used to directly map each guest address to a block of code */
  uint32_t addr_mask;

  /* runtime interface used by the backend when compiling each block's
     prologue / epilogue */
  void *data;
  int offset_pc;
  int offset_cycles;
  int offset_instrs;
  int offset_interrupts;
  void (*interrupt_check)(void *);

  /* memory interface */
  void *ctx;
  void *mem;
  struct address_space *space;
  void (*lookup)(struct address_space *, uint32_t, void **, void **,
                 mem_read_cb *, mem_write_cb *, uint32_t *);
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
  char tag[32];

  struct jit_frontend *frontend;
  struct jit_backend *backend;
  struct jit_guest *guest;
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

  /* compiled blocks */
  struct rb_tree blocks;
  struct rb_tree reverse_blocks;

  /* compiled block perf map */
  FILE *perf_map;

  /* dump ir to application directory as blocks compile */
  int dump_blocks;
};

struct jit *jit_create(const char *tag, struct jit_frontend *frontend,
                       struct jit_backend *backend, struct jit_guest *guest);
void jit_destroy(struct jit *jit);

void jit_run(struct jit *jit, int cycles);

void jit_compile_block(struct jit *jit, uint32_t guest_addr);
void jit_add_edge(struct jit *jit, void *code, uint32_t dst);

void jit_invalidate_blocks(struct jit *jit);
void jit_free_blocks(struct jit *jit);

#endif
