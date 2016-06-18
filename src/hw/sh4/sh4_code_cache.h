#ifndef SH4_CODE_CACHE_H
#define SH4_CODE_CACHE_H

#include "core/assert.h"
#include "core/rb_tree.h"

// executable code sits between 0x0c000000 and 0x0d000000 (16mb). each instr
// is 2 bytes, making for a maximum of 0x1000000 >> 1 blocks
#define BLOCK_ADDR_MASK (~0xfc000000)
#define BLOCK_OFFSET(addr) ((addr & BLOCK_ADDR_MASK) >> 1)
#define MAX_BLOCKS (0x1000000 >> 1)

struct exception_handler;
struct jit_backend;
struct jit_frontend;
struct mem_interface;

typedef uint32_t (*code_pointer_t)();

struct sh4_block {
  const uint8_t *host_addr;
  int host_size;
  uint32_t guest_addr;
  int guest_size;
  int flags;
  struct rb_node it;
  struct rb_node rit;
};

struct sh4_cache {
  struct exception_handler *exc_handler;
  struct jit_frontend *frontend;
  struct jit_backend *backend;

  code_pointer_t default_code;
  code_pointer_t code[MAX_BLOCKS];

  struct rb_tree blocks;
  struct rb_tree reverse_blocks;

  uint8_t ir_buffer[1024 * 1024];
};

struct sh4_block *sh4_cache_get_block(struct sh4_cache *cache,
                                      uint32_t guest_addr);
void sh4_cache_remove_blocks(struct sh4_cache *cache, uint32_t guest_addr);
void sh4_cache_unlink_blocks(struct sh4_cache *cache);
void sh4_cache_clear_blocks(struct sh4_cache *cache);

static inline code_pointer_t sh4_cache_get_code(struct sh4_cache *cache,
                                                uint32_t guest_addr) {
  int offset = BLOCK_OFFSET(guest_addr);
  DCHECK_LT(offset, MAX_BLOCKS);
  return cache->code[offset];
}
code_pointer_t sh4_cache_compile_code(struct sh4_cache *cache,
                                      uint32_t guest_addr, uint8_t *guest_ptr,
                                      int flags);

struct sh4_cache *sh4_cache_create(const struct mem_interface *memif,
                                   code_pointer_t default_code);
void sh4_cache_destroy(struct sh4_cache *cache);

#endif
