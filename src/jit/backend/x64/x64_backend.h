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
  RuntimeBlock *AssembleBlock(ir::IRBuilder &builder);
  void DumpBlock(RuntimeBlock *block);
  void FreeBlock(RuntimeBlock *block);

  bool HandleAccessFault(uintptr_t rip, uintptr_t fault_addr);

 private:
  X64Emitter emitter_;
};
}
}
}
}

#endif
