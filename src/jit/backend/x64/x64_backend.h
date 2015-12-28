#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include "jit/backend/backend.h"
#include "jit/backend/x64/x64_emitter.h"
#include "jit/runtime.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace x64 {

extern const Register x64_registers[];
extern const int x64_num_registers;

class X64Backend : public Backend {
 public:
  X64Backend(hw::Memory &memory);
  ~X64Backend();

  const Register *registers() const;
  int num_registers() const;

  void Reset();
  BlockRunner AssembleBlock(ir::IRBuilder &builder, void *guest_ctx);
  void DumpBlock(BlockRunner block);

  bool HandleException(sys::Exception &ex);

 private:
  X64Emitter emitter_;
};
}
}
}
}

#endif
