#ifndef RUNTIME_H
#define RUNTIME_H

#include <memory>
#include "cpu/ir/passes/pass_runner.h"
#include "emu/memory.h"

namespace dreavm {
namespace cpu {

namespace backend {
class Backend;
}

namespace frontend {
class Frontend;
}

// executable code sits between 0x0c000000 and 0x0d000000 (16mb). each instr
// is 2 bytes, making for a maximum of 0x1000000 >> 1 blocks
enum {
  BLOCK_ADDR_SHIFT = 1,
  BLOCK_ADDR_MASK = ~0xfc000000,
  MAX_BLOCKS = 0x1000000 >> BLOCK_ADDR_SHIFT,
};

static inline uint32_t BlockOffset(uint32_t addr) {
  return (addr & BLOCK_ADDR_MASK) >> BLOCK_ADDR_SHIFT;
}

class Runtime;
class RuntimeBlock;

class RuntimeBlock {
 public:
  RuntimeBlock(int guest_cycles) : guest_cycles_(guest_cycles) {}
  virtual ~RuntimeBlock() {}

  int guest_cycles() { return guest_cycles_; }

  virtual uint32_t Call(emu::Memory *memory, void *guest_ctx) = 0;
  virtual void Dump() = 0;

 private:
  int guest_cycles_;
};

class Runtime {
 public:
  Runtime(emu::Memory &memory);
  ~Runtime();

  emu::Memory &memory() { return memory_; }

  bool Init(frontend::Frontend *frontend, backend::Backend *backend);
  RuntimeBlock *ResolveBlock(uint32_t addr, const void *guest_ctx);
  void ResetBlocks();

 private:
  RuntimeBlock *CompileBlock(uint32_t addr, const void *guest_ctx);

  emu::Memory &memory_;
  frontend::Frontend *frontend_;
  backend::Backend *backend_;
  ir::passes::PassRunner pass_runner_;
  // FIXME 64 mb, could cut down to 8 mb if indices were stored instead of
  // pointers
  std::unique_ptr<RuntimeBlock> *blocks_;
  bool pending_reset_;
};
}
}

#endif
