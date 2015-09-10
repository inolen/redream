#ifndef X64_BLOCK_H
#define X64_BLOCK_H

#include "hw/memory.h"
#include "jit/backend/x64/x64_emitter.h"
#include "jit/runtime.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace x64 {

class X64Block : public RuntimeBlock {
 public:
  X64Block(int guest_cycles, X64Fn fn);

  uint32_t Call(hw::Memory *memory, void *guest_ctx);
  void Dump();

 private:
  X64Fn fn_;
};
}
}
}
}

#endif
