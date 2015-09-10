#ifndef FRONTEND_H
#define FRONTEND_H

#include <memory>
#include "hw/memory.h"
#include "jit/ir/ir_builder.h"

namespace dreavm {
namespace jit {
namespace frontend {

class Frontend {
 public:
  Frontend(hw::Memory &memory) : memory_(memory) {}
  virtual ~Frontend() {}

  virtual std::unique_ptr<ir::IRBuilder> BuildBlock(uint32_t addr,
                                                    const void *guest_ctx) = 0;

 protected:
  hw::Memory &memory_;
};
}
}
}

#endif
