#ifndef RUNTIME_H
#define RUNTIME_H

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

struct RuntimeBlock {
  RuntimeBlock()
      : call(nullptr), dump(nullptr), guest_cycles(0), priv(nullptr) {}

  uint32_t (*call)(RuntimeBlock *block, emu::Memory *memory, void *guest_ctx);
  void (*dump)(RuntimeBlock *block);
  int guest_cycles;
  void *priv;
};

class Runtime {
 public:
  Runtime(emu::Memory &memory, frontend::Frontend &frontend,
          backend::Backend &backend);
  ~Runtime();

  emu::Memory &memory() { return memory_; }

  RuntimeBlock *GetBlock(uint32_t addr, const void *guest_ctx);
  void QueueResetBlocks();

 private:
  void ResetBlocks();
  void CompileBlock(uint32_t addr, const void *guest_ctx, RuntimeBlock *block);

  emu::Memory &memory_;
  frontend::Frontend &frontend_;
  backend::Backend &backend_;
  ir::passes::PassRunner pass_runner_;
  RuntimeBlock *blocks_;
  bool pending_reset_;
};
}
}

#endif
