#ifndef SH4_CODE_CACHE_H
#define SH4_CODE_CACHE_H

#include <map>
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_context.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/ir/passes/pass_runner.h"
#include "sys/exception_handler.h"

namespace re {
namespace hw {

class Memory;

// executable code sits between 0x0c000000 and 0x0d000000 (16mb). each instr
// is 2 bytes, making for a maximum of 0x1000000 >> 1 blocks
#define BLOCK_ADDR_SHIFT 1
#define BLOCK_ADDR_MASK (~0xfc000000)
#define BLOCK_OFFSET(addr) ((addr & BLOCK_ADDR_MASK) >> BLOCK_ADDR_SHIFT)
#define MAX_BLOCKS (0x1000000 >> BLOCK_ADDR_SHIFT)

struct SH4Block;

typedef std::map<uint32_t, SH4Block *> BlockMap;
typedef std::map<const uint8_t *, SH4Block *> ReverseBlockMap;

typedef uint32_t (*CodePointer)();

struct SH4Block {
  const uint8_t *host_addr;
  int host_size;
  uint32_t guest_addr;
  int guest_size;
  int flags;
  BlockMap::iterator it;
  ReverseBlockMap::iterator rit;
};

class SH4CodeCache {
 public:
  SH4CodeCache(const jit::backend::MemoryInterface &memif,
               CodePointer default_code);
  ~SH4CodeCache();

  BlockMap::iterator blocks_begin() { return blocks_.begin(); }
  BlockMap::iterator blocks_end() { return blocks_.end(); }

  CodePointer GetCode(uint32_t guest_addr) {
    int offset = BLOCK_OFFSET(guest_addr);
    CHECK_LT(offset, MAX_BLOCKS);
    return code_[offset];
  }
  CodePointer CompileCode(uint32_t guest_addr, uint8_t *host_addr, int flags);

  SH4Block *GetBlock(uint32_t guest_addr);
  void RemoveBlocks(uint32_t guest_addr);
  void UnlinkBlocks();
  void ClearBlocks();

 private:
  static bool HandleException(void *ctx, sys::Exception &ex);

  SH4Block *LookupBlock(uint32_t guest_addr);
  SH4Block *LookupBlockReverse(const uint8_t *host_addr);
  void UnlinkBlock(SH4Block *block);
  void RemoveBlock(SH4Block *block);

  sys::ExceptionHandlerHandle eh_handle_;
  jit::frontend::Frontend *frontend_;
  jit::backend::Backend *backend_;
  jit::ir::passes::PassRunner pass_runner_;

  CodePointer default_code_;
  CodePointer code_[MAX_BLOCKS];
  BlockMap blocks_;
  ReverseBlockMap reverse_blocks_;
};
}
}

#endif
