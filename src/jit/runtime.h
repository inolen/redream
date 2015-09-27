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

class RuntimeBlock {
 public:
  RuntimeBlock(int guest_cycles) : guest_cycles_(guest_cycles) {}
  virtual ~RuntimeBlock() {}

  int guest_cycles() { return guest_cycles_; }
  virtual uint32_t Call(hw::Memory *memory, void *guest_ctx) = 0;
  virtual void Dump() = 0;

 private:
  int guest_cycles_;
};

class Runtime {
 public:
  Runtime(hw::Memory &memory, frontend::Frontend &frontend,
          backend::Backend &backend);
  ~Runtime();

  hw::Memory &memory() { return memory_; }

  RuntimeBlock *GetBlock(uint32_t addr, const void *guest_ctx);
  void ResetBlocks();

 private:
  RuntimeBlock *CompileBlock(uint32_t addr, const void *guest_ctx);

  hw::Memory &memory_;
  frontend::Frontend &frontend_;
  backend::Backend &backend_;
  ir::passes::PassRunner pass_runner_;
  RuntimeBlock **blocks_;
};
}
}

#endif
