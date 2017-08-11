#include <inttypes.h>
#include "jit/jit.h"
#include "core/core.h"
#include "core/exception_handler.h"
#include "core/filesystem.h"
#include "core/option.h"
#include "core/profiler.h"
#include "jit/ir/ir.h"
#include "jit/jit_backend.h"
#include "jit/jit_frontend.h"
#include "jit/passes/constant_propagation_pass.h"
#include "jit/passes/dead_code_elimination_pass.h"
#include "jit/passes/expression_simplification_pass.h"
#include "jit/passes/load_store_elimination_pass.h"
#include "jit/passes/register_allocation_pass.h"

#if PLATFORM_DARWIN || PLATFORM_LINUX
#include <unistd.h>
#endif

DEFINE_OPTION_INT(perf, 0, "Create maps for compiled code for use with perf");

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
  void *code = jit->backend->lookup_code(jit->backend, block->guest_addr);
  return code != block->host_addr;
}

static void jit_patch_edges(struct jit *jit, struct jit_block *block) {
  PROF_ENTER("cpu", "jit_patch_edges");

  /* patch incoming edges to this block to directly jump to it instead of
     going through dispatch */
  list_for_each_entry(edge, &block->in_edges, struct jit_edge, in_it) {
    if (!edge->patched) {
      edge->patched = 1;
      jit->backend->patch_edge(jit->backend, edge->branch,
                               edge->dst->host_addr);
    }
  }

  /* patch outgoing edges to other blocks at this time */
  list_for_each_entry(edge, &block->out_edges, struct jit_edge, out_it) {
    if (!edge->patched) {
      edge->patched = 1;
      jit->backend->patch_edge(jit->backend, edge->branch,
                               edge->dst->host_addr);
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
      jit->backend->restore_edge(jit->backend, edge->branch,
                                 edge->dst->guest_addr);
    }
  }

  PROF_LEAVE();
}

static void jit_invalidate_block(struct jit *jit, struct jit_block *block,
                                 int reason) {
  block->invalidate_reason = reason;

  jit->backend->invalidate_code(jit->backend, block->guest_addr);

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
  jit->backend->cache_code(jit->backend, block->guest_addr, block->host_addr);

  CHECK(list_empty(&block->in_edges));
  CHECK(list_empty(&block->out_edges));
}

static void jit_free_block(struct jit *jit, struct jit_block *block) {
  jit_invalidate_block(jit, block, JIT_REASON_UNKNOWN);

  free(block->source_map);
  free(block->fastmem);

  rb_unlink(&jit->blocks, &block->it, &block_map_cb);
  rb_unlink(&jit->reverse_blocks, &block->rit, &reverse_block_map_cb);

  free(block);
}

static void jit_finalize_block(struct jit *jit, struct jit_block *block) {
  CHECK(list_empty(&block->in_edges) && list_empty(&block->out_edges),
        "code shouldn't have any existing edges");
  CHECK(rb_empty_node(&block->it) && rb_empty_node(&block->rit),
        "code was already inserted in lookup tables");

  jit_cache_block(jit, block);

  rb_insert(&jit->blocks, &block->it, &block_map_cb);
  rb_insert(&jit->reverse_blocks, &block->rit, &reverse_block_map_cb);

  /* write out to perf map if enabled */
  if (OPTION_perf) {
    fprintf(jit->perf_map, "%" PRIxPTR " %x %s_0x%08x\n",
            (uintptr_t)block->host_addr, block->host_size, jit->tag,
            block->guest_addr);
  }
}

static struct jit_block *jit_alloc_block(struct jit *jit) {
  struct jit_block *block = calloc(1, sizeof(struct jit_block));
  return block;
}

void jit_free_code(struct jit *jit) {
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

void jit_invalidate_code(struct jit *jit) {
  /* invalidate code pointers, but don't remove block entries from lookup maps.
     this is used when clearing the jit while code is currently executing */
  struct rb_node *it = rb_first(&jit->blocks);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct jit_block *block = container_of(it, struct jit_block, it);
    jit_invalidate_block(jit, block, JIT_REASON_UNKNOWN);

    it = next;
  }

  /* don't reset backend code buffers, code is still running */
}

void jit_link_code(struct jit *jit, void *branch, uint32_t addr) {
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

static void jit_promote_fastmem(struct jit *jit, struct jit_block *block,
                                struct ir *ir) {
  uint32_t last_addr = block->guest_addr;

  list_for_each_entry(blk, &ir->blocks, struct ir_block, it) {
    list_for_each_entry_safe(instr, &blk->instrs, struct ir_instr, it) {
      int fastmem = block->fastmem[last_addr - block->guest_addr];

      if (instr->op == OP_SOURCE_INFO) {
        last_addr = instr->arg[0]->i32;
      } else if (instr->op == OP_LOAD_GUEST && fastmem) {
        instr->op = OP_LOAD_FAST;
      } else if (instr->op == OP_STORE_GUEST && fastmem) {
        instr->op = OP_STORE_FAST;
      }
    }
  }
}

void jit_compile_code(struct jit *jit, uint32_t guest_addr) {
  PROF_ENTER("cpu", "jit_compile_block");

#if 0
  LOG_INFO("jit_compile_block %s 0x%08x", jit->tag, guest_addr);
#endif

  /* analyze the guest code to get its extents */
  int guest_size;
  jit->frontend->analyze_code(jit->frontend, guest_addr, &guest_size);

  struct jit_block *block = jit_alloc_block(jit);
  block->guest_addr = guest_addr;
  block->guest_size = guest_size;

  /* allocate meta data structs for the original guest code */
  block->source_map = calloc(block->guest_size, sizeof(void *));
  block->fastmem = calloc(block->guest_size, sizeof(int8_t));

/* for debug builds, fastmem can be troublesome when running under gdb or
   lldb. when doing so, SIGSEGV handling can be completely disabled with:
   handle SIGSEGV nostop noprint pass
   however, then legitimate SIGSEGV will also not be handled by the debugger.
   as of this writing, there is no way to configure the debugger to ignore the
   signal initially, letting us try to handle it, and then handling it in the
   case that we do not (e.g. because it was not a fastmem-related segfault).
   because of this, fastmem is default disabled for debug builds to cause less
   headaches */
#ifdef NDEBUG
  for (int i = 0; i < block->guest_size; i++) {
    block->fastmem[i] = 1;
  }
#endif

  /* if the block had previously been invalidated, finish removing it now */
  struct jit_block *existing = jit_get_block(jit, guest_addr);

  if (existing) {
    /* if the block was invalidated due to a fastmem exception, persist its
       fastmem state */
    if (existing->invalidate_reason == JIT_REASON_FASTMEM) {
      CHECK_EQ(block->guest_size, existing->guest_size);
      memcpy(block->fastmem, existing->fastmem,
             block->guest_size * sizeof(int8_t));
    }

    jit_free_block(jit, existing);
  }

  /* translate the source machine code into ir */
  struct ir ir = {0};
  ir.buffer = jit->ir_buffer;
  ir.capacity = sizeof(jit->ir_buffer);
  jit->frontend->translate_code(jit->frontend, block, &ir);

#if 0
  jit->frontend->dump_code(jit->frontend, block);
#endif

  /* dump unoptimized block */
  if (jit->dump_code) {
    jit_dump_block(jit, guest_addr, &ir);
  }

  /* run optimization passes */
  jit_promote_fastmem(jit, block, &ir);
  lse_run(jit->lse, &ir);
  cprop_run(jit->cprop, &ir);
  esimp_run(jit->esimp, &ir);
  dce_run(jit->dce, &ir);
  ra_run(jit->ra, &ir);

  /* assemble the ir into native code */
  int res = jit->backend->assemble_code(jit->backend, block, &ir);

  if (res) {
#if 0
    jit->backend->dump_code(jit->backend, block);
#endif

    jit_finalize_block(jit, block);
  } else {
    /* if the backend overflowed, completely free the cache and let dispatch
       try to compile again */
    LOG_INFO("backend overflow, resetting code cache");
    jit_free_code(jit);
  }

  PROF_LEAVE();
}

static int jit_handle_exception(void *data, struct exception_state *ex) {
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

  /* disable fastmem optimizations for it on future compiles */
  int found = 0;
  for (int i = 0; i < block->guest_size; i++) {
    /* ignore empty entries */
    if (!block->source_map[i]) {
      continue;
    }
    if ((uintptr_t)block->source_map[i] > ex->pc) {
      break;
    }
    found = i;
  }
  block->fastmem[found] = 0;

  /* invalidate the block so it's recompiled on the next access */
  jit_invalidate_block(jit, block, JIT_REASON_FASTMEM);

  return 1;
}

void jit_run(struct jit *jit, int cycles) {
  jit->backend->run_code(jit->backend, cycles);
}

void jit_destroy(struct jit *jit) {
  if (OPTION_perf) {
    if (jit->perf_map) {
      fclose(jit->perf_map);
    }
  }

  if (jit->backend) {
    jit_free_code(jit);
  }

  if (jit->dce) {
    dce_destroy(jit->dce);
  }

  if (jit->esimp) {
    esimp_destroy(jit->esimp);
  }

  if (jit->cprop) {
    cprop_destroy(jit->cprop);
  }

  if (jit->lse) {
    lse_destroy(jit->lse);
  }

  if (jit->exc_handler) {
    exception_handler_remove(jit->exc_handler);
  }

  free(jit);
}

struct jit *jit_create(const char *tag, struct jit_frontend *frontend,
                       struct jit_backend *backend) {
  struct jit *jit = calloc(1, sizeof(struct jit));

  strncpy(jit->tag, tag, sizeof(jit->tag));
  jit->frontend = frontend;
  jit->backend = backend;

  /* create optimization passes */
  jit->lse = lse_create();
  jit->cprop = cprop_create();
  jit->esimp = esimp_create();
  jit->dce = dce_create();
  jit->ra = ra_create(jit->backend->registers, jit->backend->num_registers,
                      jit->backend->emitters, jit->backend->num_emitters);

  /* setup exception handler to deal with self-modifying code and fastmem
     related exceptions */
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

  return jit;
}
