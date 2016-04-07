#ifndef FRONTEND_H
#define FRONTEND_H

#include <memory>
#include "jit/ir/ir_builder.h"

namespace re {

namespace hw {
class Memory;
}

namespace jit {
namespace frontend {

class Frontend {
 public:
  Frontend(hw::Memory &memory, void *guest_ctx)
      : memory_(memory), guest_ctx_(guest_ctx) {}
  virtual ~Frontend() {}

  virtual ir::IRBuilder &BuildBlock(uint32_t addr, int max_instrs) = 0;

 protected:
  hw::Memory &memory_;
  void *guest_ctx_;
};
}
}
}

#endif
