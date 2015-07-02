#ifndef X64_BLOCK_H
#define X64_BLOCK_H

#include <memory>
#include "cpu/ir/ir_builder.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace x64 {

class X64Block : public RuntimeBlock {
 public:
  X64Block(int guest_cycles);
  ~X64Block();

  uint32_t Call(RuntimeContext &runtime_ctx);

 private:
};
}
}
}
}

#endif
