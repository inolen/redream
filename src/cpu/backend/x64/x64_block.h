#ifndef X64_BLOCK_H
#define X64_BLOCK_H

#include <memory>
#include "cpu/ir/ir_builder.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace x64 {

typedef void (*X64Fn)(void *guest_ctx);

class X64Block : public RuntimeBlock {
 public:
  X64Block(int guest_cycles, X64Fn fn);
  ~X64Block();

  uint32_t Call(emu::Memory *memory, void *guest_ctx);

 private:
  X64Fn fn_;
};
}
}
}
}

#endif
