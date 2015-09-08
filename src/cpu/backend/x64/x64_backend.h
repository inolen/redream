#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include <xbyak/xbyak.h>
#include "cpu/backend/backend.h"
#include "cpu/backend/x64/x64_emitter.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace x64 {

constexpr Register x64_registers[] = {{"rbx", ir::VALUE_INT_MASK},
                                      {"rbp", ir::VALUE_INT_MASK},
                                      {"r12", ir::VALUE_INT_MASK},
                                      {"r13", ir::VALUE_INT_MASK},
                                      {"r14", ir::VALUE_INT_MASK},
                                      {"r15", ir::VALUE_INT_MASK},
                                      {"xmm6", ir::VALUE_FLOAT_MASK},
                                      {"xmm7", ir::VALUE_FLOAT_MASK},
                                      {"xmm8", ir::VALUE_FLOAT_MASK},
                                      {"xmm9", ir::VALUE_FLOAT_MASK},
                                      {"xmm10", ir::VALUE_FLOAT_MASK},
                                      {"xmm11", ir::VALUE_FLOAT_MASK}};

class X64Backend : public Backend {
 public:
  X64Backend(emu::Memory &memory);
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
