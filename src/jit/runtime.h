#ifndef RUNTIME_H
#define RUNTIME_H

#include "hw/memory.h"
#include "jit/ir/passes/pass_runner.h"

namespace dreavm {
namespace jit {

namespace backend {
class Backend;
}

namespace frontend {
class Frontend;
}

// executable code sits between 0x0c000000 and 0x0d000000 (16mb). each instr
// is 2 bytes, making for a maximum of 0x1000000 >> 1 blocks
#define BLOCK_ADDR_SHIFT 1
#define BLOCK_ADDR_MASK (~0xfc000000)
#define BLOCK_OFFSET(addr) ((addr & BLOCK_ADDR_MASK) >> BLOCK_ADDR_SHIFT)
#define MAX_BLOCKS (0x1000000 >> BLOCK_ADDR_SHIFT)

// callback assigned to each block by the compiler backend. the arguments are
// somewhat bloated due to how blocks are lazily compiled as outlined below
class Runtime;
struct RuntimeBlock;
typedef uint32_t (*RuntimeBlockCall)(hw::Memory *, void *, Runtime *,
                                     RuntimeBlock *, uint32_t);

struct RuntimeBlock {
  RuntimeBlock(RuntimeBlockCall call, int guest_cycles)
      : call(call), guest_cycles(guest_cycles) {}
  virtual ~RuntimeBlock() {}

  RuntimeBlockCall call;
  int guest_cycles;
};

class Runtime {
 public:
  Runtime(hw::Memory &memory, frontend::Frontend &frontend,
          backend::Backend &backend);
  ~Runtime();

  hw::Memory &memory() { return memory_; }

  //
  // originally, GetBlock looked something like this:
  //
  // RuntimeBlock *GetBlock(uint32_t addr) {
  //   RuntimeBlock *block = blocks_[BLOCK_OFFSET(addr)];
  //   if (!block) { ... compile block ... }
  //   return block;
  // }
  //
  // however, the conditional to check for a block's existence performs very
  // poorly when called millions of times per second, and the most common case
  // is that the block already exists in the cache.
  //
  // to work around this, GetBlock has been changed to always return a valid
  // block, and the cache is initialized with all entries pointing to a special
  // lazy block. this lazy block, when called, will compile the actual block
  // update the cache to point to it.
  //
  RuntimeBlock *GetBlock(uint32_t addr) {
    int offset = BLOCK_OFFSET(addr);
    CHECK_LT(offset, MAX_BLOCKS);
    return blocks_[offset];
  }
  void ResetBlocks();

 private:
  RuntimeBlock *CompileBlock(uint32_t addr, const void *guest_ctx);
  static uint32_t LazyCompile(hw::Memory *memory, void *guest_ctx,
                              Runtime *runtime, RuntimeBlock *block,
                              uint32_t addr);

  hw::Memory &memory_;
  frontend::Frontend &frontend_;
  backend::Backend &backend_;
  ir::passes::PassRunner pass_runner_;
  RuntimeBlock **blocks_;
  RuntimeBlock lazy_block_;
};
}
}

#endif
