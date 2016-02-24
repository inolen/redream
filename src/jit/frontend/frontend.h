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
  Frontend(hw::Memory &memory) : memory_(memory) {}
  virtual ~Frontend() {}

  virtual std::unique_ptr<ir::IRBuilder> BuildBlock(uint32_t addr,
                                                    int max_instrs,
                                                    const void *guest_ctx) = 0;

 protected:
  hw::Memory &memory_;
};
}
}
}

#endif
