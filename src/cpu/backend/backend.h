#ifndef BACKEND_H
#define BACKEND_H

#include <memory>
#include "cpu/ir/ir_builder.h"
#include "emu/memory.h"

namespace dreavm {
namespace cpu {

class RuntimeBlock;

namespace backend {

class Backend {
 public:
  Backend(emu::Memory &memory) : memory_(memory) {}
  virtual ~Backend() {}

  virtual bool Init() = 0;
  virtual std::unique_ptr<RuntimeBlock> AssembleBlock(
      ir::IRBuilder &builder) = 0;

 protected:
  emu::Memory &memory_;
};
}
}
}

#endif
