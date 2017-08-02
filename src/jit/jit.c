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
#include "jit/passes/control_flow_analysis_pass.h"
#include "jit/passes/dead_code_elimination_pass.h"
#include "jit/passes/expression_simplification_pass.h"
#include "jit/passes/load_store_elimination_pass.h"
#include "jit/passes/register_allocation_pass.h"

#if PLATFORM_DARWIN || PLATFORM_LINUX
#include <unistd.h>
#endif

DEFINE_OPTION_INT(perf, 0, "Create maps for compiled code for use with perf");

static int func_map_cmp(const struct rb_node *rb_lhs,
                        const struct rb_node *rb_rhs) {
  const struct jit_func *lhs = container_of(rb_lhs, const struct jit_func, it);
  const struct jit_func *rhs = container_of(rb_rhs, const struct jit_func, it);

  if (lhs->guest_addr < rhs->guest_addr) {
    return -1;
  } else if (lhs->guest_addr > rhs->guest_addr) {
    return 1;
  } else {
    return 0;
  }
}

static int reverse_func_map_cmp(const struct rb_node *rb_lhs,
                                const struct rb_node *rb_rhs) {
  const struct jit_func *lhs = container_of(rb_lhs, const struct jit_func, rit);
  const struct jit_func *rhs = container_of(rb_rhs, const struct jit_func, rit);

  if (lhs->host_addr < rhs->host_addr) {
    return -1;
  } else if (lhs->host_addr > rhs->host_addr) {
    return 1;
  } else {
    return 0;
  }
}

static struct rb_callbacks func_map_cb = {
    &func_map_cmp, NULL, NULL,
};

static struct rb_callbacks reverse_func_map_cb = {
    &reverse_func_map_cmp, NULL, NULL,
};

static struct jit_tie *jit_get_tie(struct jit_func *func, uint32_t target) {
  int offset = (int64_t)target - (int64_t)func->guest_addr;
  CHECK(offset >= 0 && offset < func->guest_size);
  return &func->ties[offset];
}

static void jit_calc_overlap(struct jit_func *a, struct jit_func *b,
                             uint32_t *addr, int *size) {
  uint32_t a_begin = a->guest_addr;
  uint32_t a_end = a->guest_addr + a->guest_size - 1;
  uint32_t b_begin = b->guest_addr;
  uint32_t b_end = b->guest_addr + b->guest_size - 1;
  uint32_t overlap_begin = MAX(a_begin, b_begin);
  uint32_t overlap_end = MIN(a_end, b_end);
  *addr = overlap_begin;
  *size = overlap_end - overlap_begin + 1;
}

static struct jit_func *jit_lookup_func(struct jit *jit, uint32_t guest_addr) {
  struct jit_func search;
  search.guest_addr = guest_addr;

  struct rb_node *first = rb_first(&jit->funcs);
  struct rb_node *last = rb_last(&jit->funcs);
  struct rb_node *it = rb_upper_bound(&jit->funcs, &search.it, &func_map_cb);

  if (it == first) {
    return NULL;
  }

  it = it ? rb_prev(it) : last;

  struct jit_func *func = container_of(it, struct jit_func, it);
  if (guest_addr < func->guest_addr ||
      guest_addr >= func->guest_addr + func->guest_size) {
    return NULL;
  }

  return func;
}

static struct jit_func *jit_lookup_func_reverse(struct jit *jit,
                                                uint8_t *host_addr) {
  struct jit_func search;
  search.host_addr = host_addr;

  struct rb_node *first = rb_first(&jit->reverse_funcs);
  struct rb_node *last = rb_last(&jit->reverse_funcs);
  struct rb_node *rit =
      rb_upper_bound(&jit->reverse_funcs, &search.rit, &reverse_func_map_cb);

  if (rit == first) {
    return NULL;
  }

  rit = rit ? rb_prev(rit) : last;

  struct jit_func *func = container_of(rit, struct jit_func, rit);
  if (host_addr < func->host_addr ||
      host_addr >= func->host_addr + func->host_size) {
    return NULL;
  }

  return func;
}

static void jit_write_func(struct jit *jit, struct jit_func *func,
                           struct ir *ir, FILE *output) {
  ir_write(ir, output);
  fprintf(output, "\n");

  jit->frontend->dump_code(jit->frontend, func->guest_addr, func->guest_size,
                           output);
  fprintf(output, "\n");

  jit->backend->dump_code(jit->backend, func->host_addr, func->host_size,
                          output);
}

static void jit_dump_func(struct jit *jit, const char *type,
                          struct jit_func *func, struct ir *ir) {
  const char *appdir = fs_appdir();

  char irdir[PATH_MAX];
  snprintf(irdir, sizeof(irdir), "%s" PATH_SEPARATOR "%s-%s-ir", appdir,
           jit->tag, type);
  CHECK(fs_mkdir(irdir));

  char filename[PATH_MAX];
  snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "0x%08x.ir", irdir,
           func->guest_addr);

  FILE *file = fopen(filename, "w");
  CHECK_NOTNULL(file);
  jit_write_func(jit, func, ir, file);
  fclose(file);
}

static int jit_is_stale(struct jit *jit, struct jit_func *func) {
  return func->state != JIT_STATE_VALID;
}

static void jit_patch_edges(struct jit *jit, struct jit_func *func) {
  PROF_ENTER("cpu", "jit_patch_edges");

  /* patch incoming edges to this func to directly jump to it instead of
     going through dispatch */
  list_for_each_entry(edge, &func->in_edges, struct jit_edge, in_it) {
    if (edge->patched) {
      continue;
    }

    struct jit_tie *tie = jit_get_tie(edge->dst, edge->target);
    CHECK_NOTNULL(tie->block_addr);
    jit->backend->patch_edge(jit->backend, edge->branch, tie->block_addr);

    edge->patched = 1;
  }

  /* patch outgoing edges to other funcs at this time */
  list_for_each_entry(edge, &func->out_edges, struct jit_edge, out_it) {
    if (edge->patched) {
      continue;
    }

    struct jit_tie *tie = jit_get_tie(edge->dst, edge->target);
    CHECK_NOTNULL(tie->block_addr);
    jit->backend->patch_edge(jit->backend, edge->branch, tie->block_addr);

    edge->patched = 1;
  }

  PROF_LEAVE();
}

static void jit_restore_edges(struct jit *jit, struct jit_func *func) {
  PROF_ENTER("cpu", "jit_restore_edges");

  /* restore any patched branches to go back through dispatch */
  list_for_each_entry(edge, &func->in_edges, struct jit_edge, in_it) {
    if (!edge->patched) {
      continue;
    }

    jit->backend->restore_edge(jit->backend, edge->branch, edge->target);

    edge->patched = 0;
  }

  PROF_LEAVE();
}

static void jit_invalidate_func(struct jit *jit, struct jit_func *func) {
  func->state = JIT_STATE_INVALID;

  /* invalidate each block emitted by this function */
  for (int i = 0; i < func->guest_size; i++) {
    struct jit_tie *tie = &func->ties[i];
    if (!tie->block_addr) {
      continue;
    }
    uint32_t guest_block_addr = func->guest_addr + i;
    jit->backend->invalidate_code(jit->backend, guest_block_addr);
  }

  jit_restore_edges(jit, func);

  list_for_each_entry_safe(edge, &func->in_edges, struct jit_edge, in_it) {
    list_remove(&edge->src->out_edges, &edge->out_it);
    list_remove(&func->in_edges, &edge->in_it);
    free(edge);
  }

  list_for_each_entry_safe(edge, &func->out_edges, struct jit_edge, out_it) {
    list_remove(&func->out_edges, &edge->out_it);
    list_remove(&edge->dst->in_edges, &edge->in_it);
    free(edge);
  }
}

static void jit_cache_func(struct jit *jit, struct jit_func *func) {
  /* cache each block emitted by this function */
  for (int i = 0; i < func->guest_size; i++) {
    struct jit_tie *tie = &func->ties[i];
    if (!tie->block_addr) {
      continue;
    }
    uint32_t guest_block_addr = func->guest_addr + i;
    jit->backend->cache_code(jit->backend, guest_block_addr, tie->block_addr);
  }

  CHECK(list_empty(&func->in_edges));
  CHECK(list_empty(&func->out_edges));
}

static void jit_free_func(struct jit *jit, struct jit_func *func) {
  jit_invalidate_func(jit, func);

  rb_unlink(&jit->funcs, &func->it, &func_map_cb);
  rb_unlink(&jit->reverse_funcs, &func->rit, &reverse_func_map_cb);

  free(func);
}

static void jit_finalize_func(struct jit *jit, struct jit_func *func) {
  jit_cache_func(jit, func);

  rb_insert(&jit->funcs, &func->it, &func_map_cb);
  rb_insert(&jit->reverse_funcs, &func->rit, &reverse_func_map_cb);
}

static struct jit_func *jit_alloc_func(struct jit *jit, uint32_t guest_addr,
                                       int guest_size) {
  struct jit_func *func =
      calloc(1, sizeof(struct jit_func) + sizeof(struct jit_tie) * guest_size);
  func->guest_addr = guest_addr;
  func->guest_size = guest_size;

  return func;
}

void jit_free_code(struct jit *jit) {
  /* invalidate code pointers and remove block entries from lookup maps. this
     is only safe to use when no code is currently executing */
  struct rb_node *it = rb_first(&jit->funcs);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct jit_func *func = container_of(it, struct jit_func, it);
    jit_free_func(jit, func);

    it = next;
  }

  /* have the backend reset its code buffers */
  jit->backend->reset(jit->backend);
}

void jit_invalidate_code(struct jit *jit) {
  /* invalidate code pointers, but don't remove block entries from lookup maps.
     this is used when clearing the jit while code is currently executing */
  struct rb_node *it = rb_first(&jit->funcs);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct jit_func *func = container_of(it, struct jit_func, it);
    jit_invalidate_func(jit, func);

    it = next;
  }

  /* don't reset backend code buffers, code is still running */
}

void jit_link_code(struct jit *jit, void *branch, uint32_t target) {
  struct jit_func *src = jit_lookup_func_reverse(jit, branch);
  struct jit_func *dst = jit_lookup_func(jit, target);

  /* if the src is stale, or there is no dst, can't link */
  if (jit_is_stale(jit, src) || !dst) {
    return;
  }

  /* if the dst doesn't have an entry point at target, can't link */
  struct jit_tie *tie = jit_get_tie(dst, target);
  if (!tie->block_addr) {
    return;
  }

  struct jit_edge *edge = calloc(1, sizeof(struct jit_edge));
  edge->src = src;
  edge->dst = dst;
  edge->branch = branch;
  edge->target = target;
  list_add(&src->out_edges, &edge->out_it);
  list_add(&dst->in_edges, &edge->in_it);

  jit_patch_edges(jit, src);
}

static void jit_emit_callback(struct jit *jit, int type, uint32_t guest_addr,
                              uint8_t *host_addr) {
  struct jit_tie *tie = jit_get_tie(jit->curr_func, guest_addr);

  switch (type) {
    case JIT_EMIT_BLOCK:
      tie->block_addr = host_addr;
      break;
    case JIT_EMIT_INSTR:
      tie->instr_addr = host_addr;
      break;
  }
}

static struct ir_block *jit_create_entry_point(struct ir *ir, uint32_t addr) {
  /* find the first instruction for the entry point */
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      if (instr->op != OP_SOURCE_INFO) {
        continue;
      }

      if (addr == (uint32_t)instr->arg[0]->i32) {
        /* split the block starting at addr */
        struct ir_block *old_block = instr->block;
        struct ir_block *new_block = ir_split_block(ir, instr);
        if (new_block == old_block) {
          return old_block;
        }

        /* make old block branch to new block */
        struct ir_instr *last_instr =
            list_last_entry(&old_block->instrs, struct ir_instr, it);
        ir_set_current_instr(ir, last_instr);
        ir_branch(ir, ir_alloc_i32(ir, addr));
        return new_block;
      }
    }
  }

  return NULL;
}

static void jit_create_entry_points(struct jit *jit, struct ir *ir,
                                    struct jit_func *func, uint32_t pc,
                                    struct list *merged) {
  /* ensure the pc being compiled for is an entry point */
  struct ir_block *r = jit_create_entry_point(ir, pc);
  CHECK_NOTNULL(r);

  /* create entry points for each valid block being merged */
  list_for_each_entry(existing, merged, struct jit_func, invalid_it) {
    if (existing->state == JIT_STATE_INVALID) {
      continue;
    }

    uint32_t overlap_begin;
    int overlap_size;
    jit_calc_overlap(func, existing, &overlap_begin, &overlap_size);

    for (int i = 0; i < overlap_size; i++) {
      uint32_t overlap_addr = overlap_begin + i;
      struct jit_tie *func_tie = jit_get_tie(func, overlap_addr);
      struct jit_tie *existing_tie = jit_get_tie(existing, overlap_addr);

      if (!existing_tie->block_addr) {
        continue;
      }

      r = jit_create_entry_point(ir, overlap_addr);
      CHECK_NOTNULL(r);
    }
  }

  /* split blocks after each unconditional branch */
  struct ir_block *block = list_first_entry(&ir->blocks, struct ir_block, it);

  while (block) {
    struct ir_block *next_block = list_next_entry(block, struct ir_block, it);
    struct ir_instr *instr =
        list_first_entry(&block->instrs, struct ir_instr, it);

    while (instr) {
      struct ir_instr *next_instr = list_next_entry(instr, struct ir_instr, it);

      if ((instr->op == OP_BRANCH || instr->op == OP_BRANCH_COND) &&
          next_instr) {
        next_block = ir_split_block(ir, next_instr);
        break;
      }

      instr = next_instr;
    }

    block = next_block;
  }
}

static void jit_merge_extents(struct jit *jit, uint32_t *guest_addr,
                              int *guest_size, struct list *merged) {
  uint32_t base_addr = *guest_addr;
  int base_size = *guest_size;

  /* merge extents with any existing, overlapping functions */
  for (int i = 0; i < base_size; i++) {
    struct jit_func *existing = jit_lookup_func(jit, base_addr + i);

    if (!existing) {
      continue;
    }

    /* ignore functions that were explicitly invalidated */
    if (existing->state != JIT_STATE_INVALID) {
      *guest_addr = MIN(*guest_addr, existing->guest_addr);
      *guest_size = MAX(*guest_size, existing->guest_size);
    }

    list_add(merged, &existing->invalid_it);
  }
}

void jit_compile_code(struct jit *jit, uint32_t guest_addr) {
  PROF_ENTER("cpu", "jit_compile_block");

  /* analyze guest code to get its extents */
  uint32_t merged_addr = guest_addr;
  int merged_size = 0;
  jit->frontend->analyze_code(jit->frontend, merged_addr, &merged_size);

  /* merge extents with any existing functions */
  struct list merged = {0};
  jit_merge_extents(jit, &merged_addr, &merged_size, &merged);

  /* create function */
  struct jit_func *func = jit_alloc_func(jit, merged_addr, merged_size);
  jit->curr_func = func;

#if 1
  LOG_INFO("jit_compile_code tag=%s pc=0x%08x [0x%08x,0x%08x]", jit->tag,
           guest_addr, func->guest_addr,
           func->guest_addr + func->guest_size - 1);
#endif

  /* translate guest code into ir */
  struct ir ir = {0};
  ir.buffer = jit->ir_buffer;
  ir.capacity = sizeof(jit->ir_buffer);
  jit->frontend->translate_code(jit->frontend, func->guest_addr,
                                func->guest_size, &ir);

  /* perform internal ir transforms */
  jit_create_entry_points(jit, &ir, func, guest_addr, &merged);

  /* dump raw ir */
  if (jit->dump_code) {
    jit_dump_func(jit, "raw", func, &ir);
  }

  /* run optimization passes */
  cfa_run(jit->cfa, &ir);
  lse_run(jit->lse, &ir);
  cprop_run(jit->cprop, &ir);
  esimp_run(jit->esimp, &ir);
  dce_run(jit->dce, &ir);
  ra_run(jit->ra, &ir);

  /* assemble ir into host code */
  int res = jit->backend->assemble_code(jit->backend, &ir, &func->host_addr,
                                        &func->host_size,
                                        (jit_emit_cb)jit_emit_callback, jit);

  if (!res) {
    LOG_INFO("backend overflow, resetting code cache");
    jit_free_code(jit);
    PROF_LEAVE();
    return;
  }

  /* free up merged functions */
  list_for_each_entry(existing, &merged, struct jit_func, invalid_it) {
    jit_free_func(jit, existing);
  }

  /* finish by adding code to caches */
  jit_finalize_func(jit, func);

  /* dump optimized ir */
  if (jit->dump_code) {
    jit_dump_func(jit, "opt", func, &ir);
  }

#if 0
  /* write out to perf map if enabled */
  if (OPTION_perf) {
    fprintf(jit->perf_map, "%" PRIxPTR " %x %s_0x%08x\n",
            (uintptr_t)func->host_addr, func->host_size, jit->tag,
            func->guest_addr);
  }
#endif

  PROF_LEAVE();
}

static int jit_handle_exception(void *data, struct exception_state *ex) {
  struct jit *jit = data;

  /* see if there is a cached func corresponding to the current pc */
  struct jit_func *func = jit_lookup_func_reverse(jit, (void *)ex->pc);
  if (!func) {
    return 0;
  }

  /* let the backend attempt to handle the exception */
  if (!jit->backend->handle_exception(jit->backend, ex)) {
    return 0;
  }

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

  if (jit->cfa) {
    cfa_destroy(jit->cfa);
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
  jit->cfa = cfa_create();
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
