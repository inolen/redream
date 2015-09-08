#ifndef X64_BLOCK_H
#define X64_BLOCK_H

#include "cpu/backend/x64/x64_emitter.h"
#include "cpu/runtime.h"
#include "emu/memory.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace x64 {

class X64Block : public RuntimeBlock {
 public:
  X64Block(int guest_cycles, X64Fn fn);

  uint32_t Call(emu::Memory *memory, void *guest_ctx);
  void Dump();

 private:
  X64Fn fn_;
};
}
}
}
}

#endif
