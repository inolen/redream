#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include "cpu/backend/backend.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace x64 {

class X64Backend : public Backend {
 public:
  X64Backend(emu::Memory &memory);
  ~X64Backend();

  bool Init();
  std::unique_ptr<RuntimeBlock> AssembleBlock(ir::IRBuilder &builder);
};
}
}
}
}

#endif
