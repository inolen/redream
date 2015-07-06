#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include <xbyak/xbyak.h>
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

  const Register *registers() const;
  int num_registers() const;

  bool Init();
  std::unique_ptr<RuntimeBlock> AssembleBlock(ir::IRBuilder &builder);

 private:
  Xbyak::CodeGenerator gen_;
};
}
}
}
}

#endif
