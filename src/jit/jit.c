#include <inttypes.h>
#include "jit/jit.h"
#include "core/core.h"
#include "core/option.h"
#include "core/profiler.h"
#include "jit/backend/jit_backend.h"
#include "jit/frontend/jit_frontend.h"
#include "jit/ir/ir.h"
#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/expression_simplification_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "sys/exception_handler.h"
#include "sys/filesystem.h"

#if PLATFORM_DARWIN || PLATFORM_LINUX
#include <unistd.h>
#endif

DEFINE_OPTION_INT(perf, 0, "Generate perf-compatible maps for genrated code");

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

static int block_map_cmp(const struct rb_node *rb_lhs,
                         const struct rb_node *rb_rhs) {
  const struct jit_block *lhs =
      container_of(rb_lhs, const struct jit_block, it);
  const struct jit_block *rhs =
      container_of(rb_rhs, const struct jit_block, it);

  if (lhs->guest_addr < rhs->guest_addr) {
    return -1;
  } else if (lhs->guest_addr > rhs->guest_addr) {
    return 1;
  } else {
    return 0;
  }
}

static int reverse_block_map_cmp(const struct rb_node *rb_lhs,
                                 const struct rb_node *rb_rhs) {
  const struct jit_block *lhs =
      container_of(rb_lhs, const struct jit_block, rit);
  const struct jit_block *rhs =
      container_of(rb_rhs, const struct jit_block, rit);

  if ((uint8_t *)lhs->host_addr < (uint8_t *)rhs->host_addr) {
    return -1;
  } else if ((uint8_t *)lhs->host_addr > (uint8_t *)rhs->host_addr) {
    return 1;
  } else {
    return 0;
  }
}

static struct rb_callbacks block_map_cb = {
    &block_map_cmp, NULL, NULL,
};

static struct rb_callbacks reverse_block_map_cb = {
    &reverse_block_map_cmp, NULL, NULL,
};

static struct jit_block *jit_get_block(struct jit *jit, uint32_t guest_addr) {
  struct jit_block search;
  search.guest_addr = guest_addr;

  return rb_find_entry(&jit->blocks, &search, struct jit_block, it,
                       &block_map_cb);
}

static struct jit_block *jit_lookup_block_reverse(struct jit *jit,
                                                  void *host_addr) {
  struct jit_block search;
  search.host_addr = host_addr;

  struct rb_node *first = rb_first(&jit->reverse_blocks);
  struct rb_node *last = rb_last(&jit->reverse_blocks);
  struct rb_node *rit =
      rb_upper_bound(&jit->reverse_blocks, &search.rit, &reverse_block_map_cb);

  if (rit == first) {
    return NULL;
  }

  rit = rit ? rb_prev(rit) : last;

  struct jit_block *block = container_of(rit, struct jit_block, rit);
  if ((uint8_t *)host_addr < (uint8_t *)block->host_addr ||
      (uint8_t *)host_addr >=
          ((uint8_t *)block->host_addr + block->host_size)) {
    return NULL;
  }

  return block;
}

static int jit_is_stale(struct jit *jit, struct jit_block *block) {
  void *code = jit->guest->lookup_code(block->guest_addr);
  return code != block->host_addr;
}

static void jit_patch_edges(struct jit *jit, struct jit_block *block) {
  PROF_ENTER("cpu", "jit_patch_edges");

  /* patch incoming edges to this block to directly jump to it instead of
     going through dispatch */
  list_for_each_entry(edge, &block->in_edges, struct jit_edge, in_it) {
    if (!edge->patched) {
      edge->patched = 1;
      jit->guest->patch_edge(edge->branch, edge->dst->host_addr);
    }
  }

  /* patch outgoing edges to other blocks at this time */
  list_for_each_entry(edge, &block->out_edges, struct jit_edge, out_it) {
    if (!edge->patched) {
      edge->patched = 1;
      jit->guest->patch_edge(edge->branch, edge->dst->host_addr);
    }
  }

  PROF_LEAVE();
}

static void jit_restore_edges(struct jit *jit, struct jit_block *block) {
  PROF_ENTER("cpu", "jit_restore_edges");

  /* restore any patched branches to go back through dispatch */
  list_for_each_entry(edge, &block->in_edges, struct jit_edge, in_it) {
    if (edge->patched) {
      edge->patched = 0;
      jit->guest->restore_edge(edge->branch, edge->dst->guest_addr);
    }
  }

  PROF_LEAVE();
}

static void jit_invalidate_block(struct jit *jit, struct jit_block *block) {
  jit->guest->invalidate_code(block->guest_addr);

  jit_restore_edges(jit, block);

  list_for_each_entry_safe(edge, &block->in_edges, struct jit_edge, in_it) {
    list_remove(&edge->src->out_edges, &edge->out_it);
    list_remove(&block->in_edges, &edge->in_it);
    free(edge);
  }

  list_for_each_entry_safe(edge, &block->out_edges, struct jit_edge, out_it) {
    list_remove(&block->out_edges, &edge->out_it);
    list_remove(&edge->dst->in_edges, &edge->in_it);
    free(edge);
  }
}

static void jit_cache_block(struct jit *jit, struct jit_block *block) {
  jit->guest->cache_code(block->guest_addr, block->host_addr);

  CHECK(list_empty(&block->in_edges));
  CHECK(list_empty(&block->out_edges));
}

static void jit_free_block(struct jit *jit, struct jit_block *block) {
  jit_invalidate_block(jit, block);

  rb_unlink(&jit->blocks, &block->it, &block_map_cb);
  rb_unlink(&jit->reverse_blocks, &block->rit, &reverse_block_map_cb);

  free(block);
}

static struct jit_block *jit_alloc_block(struct jit *jit, uint32_t guest_addr,
                                         void *host_addr, int host_size) {
  struct jit_block *block = calloc(1, sizeof(struct jit_block));
  block->guest_addr = guest_addr;
  block->host_addr = host_addr;
  block->host_size = host_size;

  rb_insert(&jit->blocks, &block->it, &block_map_cb);
  rb_insert(&jit->reverse_blocks, &block->rit, &reverse_block_map_cb);

  jit_cache_block(jit, block);

  return block;
}

void jit_free_blocks(struct jit *jit) {
  LOG_INFO("jit_free_blocks");

  /* invalidate code pointers and remove block entries from lookup maps. this
     is only safe to use when no code is currently executing */
  struct rb_node *it = rb_first(&jit->blocks);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct jit_block *block = container_of(it, struct jit_block, it);
    jit_free_block(jit, block);

    it = next;
  }

  /* have the backend reset its code buffers */
  jit->backend->reset(jit->backend);
}

void jit_invalidate_blocks(struct jit *jit) {
  LOG_INFO("jit_invalidate_blocks");

  /* invalidate code pointers, but don't remove block entries from lookup maps.
     this is used when clearing the jit while code is currently executing */
  struct rb_node *it = rb_first(&jit->blocks);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct jit_block *block = container_of(it, struct jit_block, it);
    jit_invalidate_block(jit, block);

    it = next;
  }

  /* don't reset backend code buffers, code is still running */
}

void jit_add_edge(struct jit *jit, void *branch, uint32_t addr) {
  struct jit_block *src = jit_lookup_block_reverse(jit, branch);
  struct jit_block *dst = jit_get_block(jit, addr);

  if (jit_is_stale(jit, src) || !dst) {
    return;
  }

  struct jit_edge *edge = calloc(1, sizeof(struct jit_edge));
  edge->src = src;
  edge->dst = dst;
  edge->branch = branch;
  list_add(&src->out_edges, &edge->out_it);
  list_add(&dst->in_edges, &edge->in_it);

  jit_patch_edges(jit, src);
}

static void jit_dump_block(struct jit *jit, uint32_t guest_addr,
                           struct ir *ir) {
  const char *appdir = fs_appdir();

  char irdir[PATH_MAX];
  snprintf(irdir, sizeof(irdir), "%s" PATH_SEPARATOR "ir", appdir);
  CHECK(fs_mkdir(irdir));

  char filename[PATH_MAX];
  snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "0x%08x.ir", irdir,
           guest_addr);

  FILE *file = fopen(filename, "w");
  CHECK_NOTNULL(file);
  ir_write(ir, file);
  fclose(file);
}

void jit_compile_block(struct jit *jit, uint32_t guest_addr) {
  PROF_ENTER("cpu", "jit_compile_block");

  int fastmem = 1;

#if 0
  LOG_INFO("jit_compile_block %s 0x%08x", jit->tag, guest_addr);
#endif

  /* if the block being compiled had previously been invalidated by a fastmem
     exception, finish removing it at this time and disable fastmem opts */
  struct jit_block *existing = jit_get_block(jit, guest_addr);

  if (existing) {
    jit_free_block(jit, existing);
    fastmem = 0;
  }

  /* translate the source machine code into ir */
  struct ir ir = {0};
  ir.buffer = jit->ir_buffer;
  ir.capacity = sizeof(jit->ir_buffer);

  int guest_size;
  jit->frontend->translate_code(jit->frontend, guest_addr, &ir, fastmem,
                                &guest_size);

  /* dump unoptimized block */
  if (jit->dump_blocks) {
    jit_dump_block(jit, guest_addr, &ir);
  }

  /* run optimization passes */
  lse_run(&ir);
  esimp_run(&ir);
  dce_run(&ir);
  ra_run(&ir, jit->backend->registers, jit->backend->num_registers);

  /* assemble the ir into native code */
  int host_size = 0;
  uint8_t *host_addr =
      jit->backend->assemble_code(jit->backend, &ir, &host_size);

  if (host_addr) {
    /* cache the compiled code */
    struct jit_block *block =
        jit_alloc_block(jit, guest_addr, host_addr, host_size);

    if (OPTION_perf) {
      fprintf(jit->perf_map, "%" PRIxPTR " %x %s_0x%08x\n",
              (uintptr_t)host_addr, host_size, jit->tag, guest_addr);
    }
  } else {
    /* if the backend overflowed, completely free the cache and let dispatch
       try to compile again */
    LOG_INFO("backend overflow, resetting code cache");
    jit_free_blocks(jit);
  }

  PROF_LEAVE();
}

static int jit_handle_exception(void *data, struct exception *ex) {
  struct jit *jit = data;

  /* see if there is a cached block corresponding to the current pc */
  struct jit_block *block = jit_lookup_block_reverse(jit, (void *)ex->pc);

  if (!block) {
    return 0;
  }

  /* let the backend attempt to handle the exception */
  if (!jit->backend->handle_exception(jit->backend, ex)) {
    return 0;
  }

  /* invalidate the block so it's recompiled without fastmem optimizations
     on the next access. note, the block can't be removed from the lookup
     maps at this point because it's still executing and may raise more
     exceptions */
  jit_invalidate_block(jit, block);

  return 1;
}

int jit_init(struct jit *jit, struct jit_guest *guest,
             struct jit_frontend *frontend, struct jit_backend *backend) {
  jit->guest = guest;
  jit->frontend = frontend;
  jit->backend = backend;
  jit->exc_handler = exception_handler_add(jit, &jit_handle_exception);

  /* open perf map if enabled */
  if (OPTION_perf) {
#if PLATFORM_DARWIN || PLATFORM_LINUX
    char perf_map_path[PATH_MAX];
    snprintf(perf_map_path, sizeof(perf_map_path), "/tmp/perf-%d.map",
             getpid());
    jit->perf_map = fopen(perf_map_path, "a");
    CHECK_NOTNULL(jit->perf_map);
#endif
  }

  return 1;
}

void jit_destroy(struct jit *jit) {
  if (OPTION_perf) {
    if (jit->perf_map) {
      fclose(jit->perf_map);
    }
  }

  jit_free_blocks(jit);

  if (jit->exc_handler) {
    exception_handler_remove(jit->exc_handler);
  }

  free(jit);
}

struct jit *jit_create(const char *tag) {
  struct jit *jit = calloc(1, sizeof(struct jit));

  strncpy(jit->tag, tag, sizeof(jit->tag));

  return jit;
}
