#ifndef SH4_CODE_CACHE_H
#define SH4_CODE_CACHE_H

#include "core/assert.h"
#include "core/rb_tree.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_context.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "sys/exception_handler.h"

// executable code sits between 0x0c000000 and 0x0d000000 (16mb). each instr
// is 2 bytes, making for a maximum of 0x1000000 >> 1 blocks
#define BLOCK_ADDR_SHIFT 1
#define BLOCK_ADDR_MASK (~0xfc000000)
#define BLOCK_OFFSET(addr) ((addr & BLOCK_ADDR_MASK) >> BLOCK_ADDR_SHIFT)
#define MAX_BLOCKS (0x1000000 >> BLOCK_ADDR_SHIFT)

struct jit_backend_s;
struct mem_interface_s;
struct sh4_block_s;

typedef uint32_t (*code_pointer_t)();

typedef struct sh4_block_s {
  rb_node_t it;
  rb_node_t rit;
  const uint8_t *host_addr;
  int host_size;
  uint32_t guest_addr;
  int guest_size;
  int flags;
} sh4_block_t;

typedef struct sh4_cache_s {
  struct re_exception_handler_s *eh_handle;
  struct jit_frontend_s *frontend;
  struct jit_backend_s *backend;

  uint8_t ir_buffer[1024 * 1024];

  code_pointer_t default_code;
  code_pointer_t code[MAX_BLOCKS];

  rb_tree_t blocks;
  rb_tree_t reverse_blocks;
} sh4_cache_t;

struct sh4_cache_s *sh4_cache_create(const struct mem_interface_s *memif,
                                     code_pointer_t default_code);
void sh4_cache_destroy(struct sh4_cache_s *cache);

static inline code_pointer_t sh4_cache_get_code(struct sh4_cache_s *cache,
                                                uint32_t guest_addr) {
  int offset = BLOCK_OFFSET(guest_addr);
  CHECK_LT(offset, MAX_BLOCKS);
  return cache->code[offset];
}
code_pointer_t sh4_cache_compile_code(struct sh4_cache_s *cache,
                                      uint32_t guest_addr, uint8_t *guest_ptr,
                                      int flags);

sh4_block_t *sh4_cache_get_block(struct sh4_cache_s *cache,
                                 uint32_t guest_addr);
void sh4_cache_remove_blocks(struct sh4_cache_s *cache, uint32_t guest_addr);
void sh4_cache_unlink_blocks(struct sh4_cache_s *cache);
void sh4_cache_clear_blocks(struct sh4_cache_s *cache);

#endif
