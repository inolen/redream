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
  Runtime(emu::Memory &memory, frontend::Frontend &frontend,
          backend::Backend &backend);
  ~Runtime();

  emu::Memory &memory() { return memory_; }

  RuntimeBlock *GetBlock(uint32_t addr, const void *guest_ctx);
  void ResetBlocks();

 private:
  RuntimeBlock *CompileBlock(uint32_t addr, const void *guest_ctx);

  emu::Memory &memory_;
  frontend::Frontend &frontend_;
  backend::Backend &backend_;
  ir::passes::PassRunner pass_runner_;
  std::unique_ptr<RuntimeBlock> *blocks_;
};
}
}

#endif
