#ifndef RUNTIME_H
#define RUNTIME_H

#include <memory>
#include "cpu/ir/pass_runner.h"
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

struct RuntimeContext {
  RuntimeContext() : runtime(nullptr), guest_ctx(nullptr) {}

  Runtime *runtime;
  void *guest_ctx;
  uint8_t (*R8)(RuntimeContext *, uint32_t);
  uint16_t (*R16)(RuntimeContext *, uint32_t);
  uint32_t (*R32)(RuntimeContext *, uint32_t);
  uint64_t (*R64)(RuntimeContext *, uint32_t);
  float (*RF32)(RuntimeContext *, uint32_t);
  double (*RF64)(RuntimeContext *, uint32_t);
  void (*W8)(RuntimeContext *, uint32_t, uint8_t);
  void (*W16)(RuntimeContext *, uint32_t, uint16_t);
  void (*W32)(RuntimeContext *, uint32_t, uint32_t);
  void (*W64)(RuntimeContext *, uint32_t, uint64_t);
  void (*WF32)(RuntimeContext *, uint32_t, float);
  void (*WF64)(RuntimeContext *, uint32_t, double);
};

class RuntimeBlock {
 public:
  RuntimeBlock(int guest_cycles) : guest_cycles_(guest_cycles) {}
  virtual ~RuntimeBlock() {}

  int guest_cycles() { return guest_cycles_; }

  virtual uint32_t Call(RuntimeContext &runtime_ctx) = 0;

 private:
  int guest_cycles_;
};

class Runtime {
 public:
  Runtime(emu::Memory &memory);
  ~Runtime();

  emu::Memory &memory() { return memory_; }
  RuntimeContext &ctx() { return runtime_ctx_; }

  bool Init(frontend::Frontend *frontend, backend::Backend *backend);
  RuntimeBlock *ResolveBlock(uint32_t addr);
  void ResetBlocks();

 private:
  uint32_t ResolveAddress(uint32_t addr);
  RuntimeBlock *CompileBlock(uint32_t addr);

  emu::Memory &memory_;
  frontend::Frontend *frontend_;
  backend::Backend *backend_;
  RuntimeContext runtime_ctx_;
  ir::PassRunner pass_runner_;
  // FIXME 64 mb, could cut down to 8 mb if indices were stored instead of
  // pointers
  std::unique_ptr<RuntimeBlock> *blocks_;
  bool pending_reset_;
};
}
}

#endif
