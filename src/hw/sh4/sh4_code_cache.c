#include "core/core.h"
#include "core/profiler.h"
#include "hw/sh4/sh4_code_cache.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_analyze.h"
#include "jit/frontend/sh4/sh4_frontend.h"
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

static int block_map_cmp(const rb_node_t *lhs_it, const rb_node_t *rhs_it) {
  const sh4_block_t *lhs = container_of(lhs_it, const sh4_block_t, it);
  const sh4_block_t *rhs = container_of(rhs_it, const sh4_block_t, it);

  return (int)((int64_t)lhs->guest_addr - (int64_t)rhs->guest_addr);
}

static int reverse_block_map_cmp(const rb_node_t *lhs_it,
                                 const rb_node_t *rhs_it) {
  const sh4_block_t *lhs = container_of(lhs_it, const sh4_block_t, rit);
  const sh4_block_t *rhs = container_of(rhs_it, const sh4_block_t, rit);

  return (int)(lhs->host_addr - rhs->host_addr);
}

static rb_callback_t block_map_cb = {
    &block_map_cmp, NULL, NULL,
};

static rb_callback_t reverse_block_map_cb = {
    &reverse_block_map_cmp, NULL, NULL,
};

static void sh4_cache_unlink_block(sh4_cache_t *cache, sh4_block_t *block) {
  cache->code[BLOCK_OFFSET(block->guest_addr)] = cache->default_code;
}

static void sh4_cache_remove_block(sh4_cache_t *cache, sh4_block_t *block) {
  sh4_cache_unlink_block(cache, block);

  rb_unlink(&cache->blocks, &block->it, &block_map_cb);
  rb_unlink(&cache->reverse_blocks, &block->rit, &reverse_block_map_cb);

  free(block);
}

static sh4_block_t *sh4_cache_lookup_block(sh4_cache_t *cache,
                                           uint32_t guest_addr) {
  // find the first block who's address is greater than guest_addr
  sh4_block_t search;
  search.guest_addr = guest_addr;

  rb_node_t *first = rb_first(&cache->blocks);
  rb_node_t *last = rb_last(&cache->blocks);
  rb_node_t *it = rb_upper_bound(&cache->blocks, &search.it, &block_map_cb);

  // if all addresses are greater than guest_addr, there is no block
  // for this address
  if (it == first) {
    return NULL;
  }

  // the actual block is the previous one
  it = it ? rb_prev(it) : last;

  sh4_block_t *block = container_of(it, sh4_block_t, it);
  return block;
}

static sh4_block_t *sh4_cache_lookup_block_reverse(sh4_cache_t *cache,
                                                   const uint8_t *host_addr) {
  sh4_block_t search;
  search.host_addr = host_addr;

  rb_node_t *first = rb_first(&cache->reverse_blocks);
  rb_node_t *last = rb_last(&cache->reverse_blocks);
  rb_node_t *rit = rb_upper_bound(&cache->reverse_blocks, &search.rit,
                                  &reverse_block_map_cb);

  if (rit == first) {
    return NULL;
  }

  rit = rit ? rb_prev(rit) : last;

  sh4_block_t *block = container_of(rit, sh4_block_t, rit);
  return block;
}

static bool sh4_cache_handle_exception(sh4_cache_t *cache, re_exception_t *ex) {
  // see if there is an assembled block corresponding to the current pc
  sh4_block_t *block =
      sh4_cache_lookup_block_reverse(cache, (const uint8_t *)ex->pc);

  if (!block) {
    return false;
  }

  // let the backend attempt to handle the exception
  if (!cache->backend->handle_exception(cache->backend, ex)) {
    return false;
  }

  // exception was handled, unlink the code pointer and flag the block to be
  // recompiled without fastmem optimizations on the next access. note, the
  // block can't be removed from the lookup maps at this point because it's
  // still executing and may trigger subsequent exceptions
  sh4_cache_unlink_block(cache, block);

  block->flags |= SH4_SLOWMEM;

  return true;
}

static code_pointer_t sh4_cache_compile_code_inner(sh4_cache_t *cache,
                                                   uint32_t guest_addr,
                                                   uint8_t *guest_ptr,
                                                   int flags) {
  int offset = BLOCK_OFFSET(guest_addr);
  CHECK_LT(offset, MAX_BLOCKS);
  code_pointer_t *code = &cache->code[offset];

  // make sure there's not a valid code pointer
  CHECK_EQ(*code, cache->default_code);

  // if the block being compiled had previously been unlinked by a
  // fastmem exception, reuse the block's flags and finish removing
  // it at this time;
  sh4_block_t search;
  search.guest_addr = guest_addr;

  sh4_block_t *unlinked =
      rb_find_entry(&cache->blocks, &search, it, &block_map_cb);

  if (unlinked) {
    flags |= unlinked->flags;

    sh4_cache_remove_block(cache, unlinked);
  }

  // translate the SH4 into IR
  ir_t ir = {};
  ir.buffer = cache->ir_buffer;
  ir.capacity = sizeof(cache->ir_buffer);

  int guest_size = 0;
  cache->frontend->translate_code(cache->frontend, guest_addr, guest_ptr, flags,
                                  &guest_size, &ir);

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
  ra_run(&ir, cache->backend->registers, cache->backend->num_registers);

  // assemble the IR into native code
  int host_size = 0;
  const uint8_t *host_addr =
      cache->backend->assemble_code(cache->backend, &ir, &host_size);

  if (!host_addr) {
    LOG_INFO("Assembler overflow, resetting block cache");

    // the backend overflowed, completely clear the block cache
    sh4_cache_clear_blocks(cache);

    // if the backend fails to assemble on an empty cache, there's nothing to be
    // done
    host_addr = cache->backend->assemble_code(cache->backend, &ir, &host_size);

    CHECK(host_addr, "Backend assembler buffer overflow");
  }

  // allocate the new block
  sh4_block_t *block = calloc(1, sizeof(sh4_block_t));
  block->host_addr = host_addr;
  block->host_size = host_size;
  block->guest_addr = guest_addr;
  block->guest_size = guest_size;
  block->flags = flags;
  rb_insert(&cache->blocks, &block->it, &block_map_cb);
  rb_insert(&cache->reverse_blocks, &block->rit, &reverse_block_map_cb);

  // update code pointer
  *code = (code_pointer_t)block->host_addr;

  return *code;
}

sh4_block_t *sh4_cache_get_block(sh4_cache_t *cache, uint32_t guest_addr) {
  sh4_block_t search;
  search.guest_addr = guest_addr;

  return rb_find_entry(&cache->blocks, &search, it, &block_map_cb);
}

void sh4_cache_remove_blocks(sh4_cache_t *cache, uint32_t guest_addr) {
  // remove any block which overlaps the address
  while (true) {
    sh4_block_t *block = sh4_cache_lookup_block(cache, guest_addr);

    if (!block) {
      break;
    }

    sh4_cache_remove_block(cache, block);
  }
}

void sh4_cache_unlink_blocks(sh4_cache_t *cache) {
  // unlink all code pointers, but don't remove the block entries. this is used
  // when clearing the cache while code is currently executing
  rb_node_t *it = rb_first(&cache->blocks);

  while (it) {
    rb_node_t *next = rb_next(it);

    sh4_block_t *block = container_of(it, sh4_block_t, it);
    sh4_cache_unlink_block(cache, block);

    it = next;
  }
}

void sh4_cache_clear_blocks(sh4_cache_t *cache) {
  // unlink all code pointers and remove all block entries. this is only safe to
  // use when no code is currently executing
  rb_node_t *it = rb_first(&cache->blocks);

  while (it) {
    rb_node_t *next = rb_next(it);

    sh4_block_t *block = container_of(it, sh4_block_t, it);
    sh4_cache_remove_block(cache, block);

    it = next;
  }

  // have the backend reset its codegen buffers as well
  cache->backend->reset(cache->backend);
}

code_pointer_t sh4_cache_compile_code(sh4_cache_t *cache, uint32_t guest_addr,
                                      uint8_t *guest_ptr, int flags) {
  prof_enter("sh4_cache_compile_code");
  code_pointer_t code =
      sh4_cache_compile_code_inner(cache, guest_addr, guest_ptr, flags);
  prof_leave();
  return code;
}

sh4_cache_t *sh4_cache_create(const mem_interface_t *memif,
                              code_pointer_t default_code) {
  sh4_cache_t *cache = calloc(1, sizeof(sh4_cache_t));

  // add exception handler to help recompile blocks when protected memory is
  // accessed
  cache->exc_handler = exception_handler_add(
      cache, (exception_handler_cb)&sh4_cache_handle_exception);

  // setup parser and emitter
  cache->frontend = sh4_frontend_create();
  cache->backend = x64_backend_create(memif);

  // initialize all entries in block cache to reference the default block
  cache->default_code = default_code;

  for (int i = 0; i < MAX_BLOCKS; i++) {
    cache->code[i] = default_code;
  }

  return cache;
}

void sh4_cache_destroy(sh4_cache_t *cache) {
  exception_handler_remove(cache->exc_handler);
  sh4_frontend_destroy(cache->frontend);
  x64_backend_destroy(cache->backend);
  free(cache);
}
