#ifndef FRONTEND_H
#define FRONTEND_H

#include <memory>
#include "cpu/ir/ir_builder.h"
#include "emu/memory.h"

namespace dreavm {
namespace cpu {
namespace frontend {

class Frontend {
 public:
  Frontend(emu::Memory &memory) : memory_(memory) {}
  virtual ~Frontend() {}

  virtual bool Init() = 0;
  virtual std::unique_ptr<ir::IRBuilder> BuildBlock(uint32_t addr,
                                                    const void *guest_ctx) = 0;

 protected:
  emu::Memory &memory_;
};
}
}
}

#endif
