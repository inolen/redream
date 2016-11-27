#include <inttypes.h>
#include "jit/jit.h"
#include "core/core.h"
#include "core/option.h"
#include "core/profiler.h"
#include "jit/backend/backend.h"
#include "jit/frontend/frontend.h"
#include "jit/ir/ir.h"
#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "sys/exception_handler.h"
#include "sys/filesystem.h"

#if PLATFORM_DARWIN || PLATFORM_LINUX
#include <unistd.h>
#endif

DEFINE_OPTION_BOOL(perf, false,
                   "Generate perf-compatible maps for genrated code");

static bool jit_handle_exception(void *data, struct exception *ex);

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

  if (lhs->host_addr < rhs->host_addr) {
    return -1;
  } else if (lhs->host_addr > rhs->host_addr) {
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

int jit_init(struct jit *jit, struct jit_frontend *frontend,
             struct jit_backend *backend, code_pointer_t default_code) {
  jit->frontend = frontend;
  jit->backend = backend;

  /* add handler to invalidate blocks when protected memory is accessed */
  jit->exc_handler = exception_handler_add(jit, &jit_handle_exception);

  /* initialize the dispatch cache */
  jit->default_code = default_code;
  jit->code = malloc(jit->block_max * sizeof(struct jit_block));
  for (int i = 0; i < jit->block_max; i++) {
    jit->code[i] = default_code;
  }

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

struct jit *jit_create(const char *tag) {
  struct jit *jit = calloc(1, sizeof(struct jit));

  strncpy(jit->tag, tag, sizeof(jit->tag));

  return jit;
}

void jit_destroy(struct jit *jit) {
  if (OPTION_perf) {
    if (jit->perf_map) {
      fclose(jit->perf_map);
    }
  }
  jit_clear_blocks(jit);
  free(jit->code);
  exception_handler_remove(jit->exc_handler);
  free(jit);
}

static inline int jit_block_offset(struct jit *jit, uint32_t addr) {
  return (addr & jit->block_mask) >> jit->block_shift;
}

static void jit_unlink_block(struct jit *jit, struct jit_block *block) {
  int code_idx = jit_block_offset(jit, block->guest_addr);
  jit->code[code_idx] = jit->default_code;
}

static void jit_remove_block(struct jit *jit, struct jit_block *block) {
  jit_unlink_block(jit, block);

  rb_unlink(&jit->blocks, &block->it, &block_map_cb);
  rb_unlink(&jit->reverse_blocks, &block->rit, &reverse_block_map_cb);

  free(block);
}

static struct jit_block *jit_lookup_block(struct jit *jit,
                                          uint32_t guest_addr) {
  /* find the first block who's address is greater than guest_addr */
  struct jit_block search;
  search.guest_addr = guest_addr;

  struct rb_node *first = rb_first(&jit->blocks);
  struct rb_node *last = rb_last(&jit->blocks);
  struct rb_node *it = rb_upper_bound(&jit->blocks, &search.it, &block_map_cb);

  /* if all addresses are greater than guest_addr, there is no block */
  if (it == first) {
    return NULL;
  }

  /* the actual block is the previous one */
  it = it ? rb_prev(it) : last;

  struct jit_block *block = container_of(it, struct jit_block, it);
  return block;
}

static struct jit_block *jit_lookup_block_reverse(struct jit *jit,
                                                  const uint8_t *host_addr) {
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
  return block;
}

struct jit_block *jit_get_block(struct jit *jit, uint32_t guest_addr) {
  struct jit_block search;
  search.guest_addr = guest_addr;

  return rb_find_entry(&jit->blocks, &search, struct jit_block, it,
                       &block_map_cb);
}

void jit_remove_blocks(struct jit *jit, uint32_t guest_addr) {
  /* remove any block which overlaps the address */
  while (1) {
    struct jit_block *block = jit_lookup_block(jit, guest_addr);

    if (!block) {
      break;
    }

    jit_remove_block(jit, block);
  }
}

void jit_unlink_blocks(struct jit *jit) {
  /*
   * unlink all code pointers, but don't remove the block entries. this is used
   * when clearing the jit while code is currently executing
   */
  struct rb_node *it = rb_first(&jit->blocks);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct jit_block *block = container_of(it, struct jit_block, it);
    jit_unlink_block(jit, block);

    it = next;
  }
}

void jit_clear_blocks(struct jit *jit) {
  /*
   * unlink all code pointers and remove all block entries. this is only safe
   * to use when no code is currently executing
   */
  struct rb_node *it = rb_first(&jit->blocks);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct jit_block *block = container_of(it, struct jit_block, it);
    jit_remove_block(jit, block);

    it = next;
  }

  /* have the backend reset its codegen buffers as well */
  jit->backend->reset(jit->backend);
}

code_pointer_t jit_compile_code(struct jit *jit, uint32_t guest_addr) {
  PROF_ENTER("cpu", "jit_compile_code");

  int code_idx = jit_block_offset(jit, guest_addr);
  code_pointer_t *code = &jit->code[code_idx];

  /* make sure there's not a valid code pointer */
  CHECK_EQ(*code, jit->default_code);

  /*
   * if the block being compiled had previously been unlinked by a
   * fastmem exception, reuse the block's flags and finish removing
   * it at this time
   */
  int fastmem = 1;

  struct jit_block search;
  search.guest_addr = guest_addr;

  struct jit_block *unlinked =
      rb_find_entry(&jit->blocks, &search, struct jit_block, it, &block_map_cb);

  if (unlinked) {
    jit_remove_block(jit, unlinked);
    fastmem = 0;
  }

  /* translate the source machine code into ir */
  struct ir ir = {0};
  ir.buffer = jit->ir_buffer;
  ir.capacity = sizeof(jit->ir_buffer);

  jit->frontend->translate_code(jit->frontend, guest_addr, &ir, fastmem);

#if 0
  const char *appdir = fs_appdir();

  char irdir[PATH_MAX];
  snprintf(irdir, sizeof(irdir), "%s" PATH_SEPARATOR "ir", appdir);
  fs_mkdir(irdir);

  char filename[PATH_MAX];
  snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "0x%08x.ir", irdir, guest_addr);

  std::ofstream output(filename);
  builder.Dump(output);
#endif

  /* run optimization passes */
  lse_run(&ir);
  dce_run(&ir);
  ra_run(&ir, jit->backend->registers, jit->backend->num_registers);

  /* assemble the ir into native code */
  int host_size = 0;
  const uint8_t *host_addr =
      jit->backend->assemble_code(jit->backend, &ir, &host_size);

  if (!host_addr) {
    LOG_INFO("backend overflow, resetting code cache");

    /* the backend overflowed, completely clear the code cache */
    jit_clear_blocks(jit);

    /* if the backend fails to assemble on an empty cache, abort */
    host_addr = jit->backend->assemble_code(jit->backend, &ir, &host_size);

    CHECK(host_addr, "Backend assembler buffer overflow");
  }

  /* allocate the new block */
  struct jit_block *block = calloc(1, sizeof(struct jit_block));
  block->host_addr = host_addr;
  block->host_size = host_size;
  block->guest_addr = guest_addr;
  rb_insert(&jit->blocks, &block->it, &block_map_cb);
  rb_insert(&jit->reverse_blocks, &block->rit, &reverse_block_map_cb);

  /* update code pointer */
  *code = (code_pointer_t)block->host_addr;

  if (OPTION_perf) {
    fprintf(jit->perf_map, "%" PRIxPTR " %x %s_0x%08x\n", (uintptr_t)host_addr,
            host_size, jit->tag, guest_addr);
  }

  PROF_LEAVE();

  return *code;
}

static bool jit_handle_exception(void *data, struct exception *ex) {
  struct jit *jit = data;

  /* see if there is an assembled block corresponding to the current pc */
  struct jit_block *block =
      jit_lookup_block_reverse(jit, (const uint8_t *)ex->pc);

  if (!block) {
    return false;
  }

  /* let the backend attempt to handle the exception */
  if (!jit->backend->handle_exception(jit->backend, ex)) {
    return false;
  }

  /*
   * exception was handled, unlink the code pointer and flag the block to be
   * recompiled without fastmem optimizations on the next access. note, the
   * block can't be removed from the lookup maps at this point because it's
   * still executing and may trigger more exceptions
   */
  jit_unlink_block(jit, block);

  return true;
}
