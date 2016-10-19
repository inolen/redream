#include "jit/jit.h"
#include "core/core.h"
#include "core/profiler.h"
#include "jit/backend/backend.h"
#include "jit/frontend/frontend.h"
#include "jit/ir/ir.h"
// #include "jit/ir/passes/constant_propagation_pass.h"
// #include "jit/ir/passes/conversion_elimination_pass.h"
#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "sys/exception_handler.h"
#include "sys/filesystem.h"

#define BLOCK_OFFSET(addr) \
  ((addr & jit->guest.block_mask) >> jit->guest.block_shift)

static int block_map_cmp(const struct rb_node *rb_lhs,
                         const struct rb_node *rb_rhs) {
  const struct jit_block *lhs =
      container_of(rb_lhs, const struct jit_block, it);
  const struct jit_block *rhs =
      container_of(rb_rhs, const struct jit_block, it);

  return (int)((int64_t)lhs->guest_addr - (int64_t)rhs->guest_addr);
}

static int reverse_block_map_cmp(const struct rb_node *rb_lhs,
                                 const struct rb_node *rb_rhs) {
  const struct jit_block *lhs =
      container_of(rb_lhs, const struct jit_block, rit);
  const struct jit_block *rhs =
      container_of(rb_rhs, const struct jit_block, rit);

  return (int)(lhs->host_addr - rhs->host_addr);
}

static struct rb_callbacks block_map_cb = {
    &block_map_cmp, NULL, NULL,
};

static struct rb_callbacks reverse_block_map_cb = {
    &reverse_block_map_cmp, NULL, NULL,
};

static void jit_unlink_block(struct jit *jit, struct jit_block *block) {
  jit->code[BLOCK_OFFSET(block->guest_addr)] = jit->default_code;
}

static void jit_remove_block(struct jit *jit, struct jit_block *block) {
  jit_unlink_block(jit, block);

  rb_unlink(&jit->blocks, &block->it, &block_map_cb);
  rb_unlink(&jit->reverse_blocks, &block->rit, &reverse_block_map_cb);

  free(block);
}

static struct jit_block *jit_lookup_block(struct jit *jit,
                                          uint32_t guest_addr) {
  // find the first block who's address is greater than guest_addr
  struct jit_block search;
  search.guest_addr = guest_addr;

  struct rb_node *first = rb_first(&jit->blocks);
  struct rb_node *last = rb_last(&jit->blocks);
  struct rb_node *it = rb_upper_bound(&jit->blocks, &search.it, &block_map_cb);

  // if all addresses are greater than guest_addr, there is no block
  // for this address
  if (it == first) {
    return NULL;
  }

  // the actual block is the previous one
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

static bool jit_handle_exception(void *data, struct exception *ex) {
  struct jit *jit = data;

  // see if there is an assembled block corresponding to the current pc
  struct jit_block *block =
      jit_lookup_block_reverse(jit, (const uint8_t *)ex->pc);

  if (!block) {
    return false;
  }

  // let the backend attempt to handle the exception
  if (!jit->backend->handle_exception(jit->backend, ex)) {
    return false;
  }

  // exception was handled, unlink the code pointer and flag the block to be
  // recompiled without fastmem optimizations on the next access. note, the
  // block can't be removed from the lookup maps at this point because it's
  // still executing and may trigger subsequent exceptions
  jit_unlink_block(jit, block);

  block->flags |= JIT_SLOWMEM;

  return true;
}

static code_pointer_t jit_compile_code_inner(struct jit *jit,
                                             uint32_t guest_addr, int flags) {
  code_pointer_t *code = &jit->code[BLOCK_OFFSET(guest_addr)];

  // make sure there's not a valid code pointer
  CHECK_EQ(*code, jit->default_code);

  // if the block being compiled had previously been unlinked by a
  // fastmem exception, reuse the block's flags and finish removing
  // it at this time;
  struct jit_block search;
  search.guest_addr = guest_addr;

  struct jit_block *unlinked =
      rb_find_entry(&jit->blocks, &search, struct jit_block, it, &block_map_cb);

  if (unlinked) {
    flags |= unlinked->flags;

    jit_remove_block(jit, unlinked);
  }

  // translate the source machine code into IR
  struct ir ir = {0};
  ir.buffer = jit->ir_buffer;
  ir.capacity = sizeof(jit->ir_buffer);

  int guest_size = 0;
  jit->frontend->translate_code(jit->frontend, guest_addr, flags, &guest_size,
                                &ir);

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

  // run optimization passes
  lse_run(&ir);
  dce_run(&ir);
  ra_run(&ir, jit->backend->registers, jit->backend->num_registers);

  // assemble the IR into native code
  int host_size = 0;
  const uint8_t *host_addr =
      jit->backend->assemble_code(jit->backend, &ir, &host_size);

  if (!host_addr) {
    LOG_INFO("Assembler overflow, resetting block jit");

    // the backend overflowed, completely clear the block jit
    jit_clear_blocks(jit);

    // if the backend fails to assemble on an empty jit, there's nothing to be
    // done
    host_addr = jit->backend->assemble_code(jit->backend, &ir, &host_size);

    CHECK(host_addr, "Backend assembler buffer overflow");
  }

  // allocate the new block
  struct jit_block *block = calloc(1, sizeof(struct jit_block));
  block->host_addr = host_addr;
  block->host_size = host_size;
  block->guest_addr = guest_addr;
  block->guest_size = guest_size;
  block->flags = flags;
  rb_insert(&jit->blocks, &block->it, &block_map_cb);
  rb_insert(&jit->reverse_blocks, &block->rit, &reverse_block_map_cb);

  // update code pointer
  *code = (code_pointer_t)block->host_addr;

  return *code;
}

struct jit_block *jit_get_block(struct jit *jit, uint32_t guest_addr) {
  struct jit_block search;
  search.guest_addr = guest_addr;

  return rb_find_entry(&jit->blocks, &search, struct jit_block, it,
                       &block_map_cb);
}

void jit_remove_blocks(struct jit *jit, uint32_t guest_addr) {
  // remove any block which overlaps the address
  while (true) {
    struct jit_block *block = jit_lookup_block(jit, guest_addr);

    if (!block) {
      break;
    }

    jit_remove_block(jit, block);
  }
}

void jit_unlink_blocks(struct jit *jit) {
  // unlink all code pointers, but don't remove the block entries. this is used
  // when clearing the jit while code is currently executing
  struct rb_node *it = rb_first(&jit->blocks);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct jit_block *block = container_of(it, struct jit_block, it);
    jit_unlink_block(jit, block);

    it = next;
  }
}

void jit_clear_blocks(struct jit *jit) {
  // unlink all code pointers and remove all block entries. this is only safe to
  // use when no code is currently executing
  struct rb_node *it = rb_first(&jit->blocks);

  while (it) {
    struct rb_node *next = rb_next(it);

    struct jit_block *block = container_of(it, struct jit_block, it);
    jit_remove_block(jit, block);

    it = next;
  }

  // have the backend reset its codegen buffers as well
  jit->backend->reset(jit->backend);
}

code_pointer_t jit_compile_code(struct jit *jit, uint32_t guest_addr,
                                int flags) {
  prof_enter("jit_compile_code");
  code_pointer_t code = jit_compile_code_inner(jit, guest_addr, flags);
  prof_leave();
  return code;
}

struct jit *jit_create(struct jit_guest *guest, struct jit_frontend *frontend,
                       struct jit_backend *backend,
                       code_pointer_t default_code) {
  struct jit *jit = calloc(1, sizeof(struct jit));

  jit->guest = *guest;
  jit->frontend = frontend;
  jit->backend = backend;

  // add exception handler to help recompile blocks when protected memory is
  // accessed
  jit->exc_handler = exception_handler_add(jit, &jit_handle_exception);

  // initialize all entries in block jit to reference the default block
  jit->default_code = default_code;
  jit->code = malloc(jit->guest.max_blocks * sizeof(struct jit_block));

  for (int i = 0; i < jit->guest.max_blocks; i++) {
    jit->code[i] = default_code;
  }

  return jit;
}

void jit_destroy(struct jit *jit) {
  jit_clear_blocks(jit);
  free(jit->code);
  exception_handler_remove(jit->exc_handler);
  free(jit);
}
