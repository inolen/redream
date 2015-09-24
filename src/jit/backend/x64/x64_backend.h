#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include <xbyak/xbyak.h>
#include "jit/backend/backend.h"
#include "jit/backend/x64/x64_emitter.h"
#include "jit/runtime.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace x64 {

class X64Backend : public Backend {
 public:
  X64Backend(hw::Memory &memory);
  ~X64Backend();

  const Register *registers() const;
  int num_registers() const;

  void Reset();
  std::unique_ptr<RuntimeBlock> AssembleBlock(ir::IRBuilder &builder);

 private:
  Xbyak::CodeGenerator codegen_;
  X64Emitter emitter_;
};
}
}
}
}

#endif
