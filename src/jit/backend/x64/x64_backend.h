#ifndef X64_BACKEND_H
#define X64_BACKEND_H

#include "jit/backend/backend.h"
#include "jit/backend/x64/x64_emitter.h"

namespace re {
namespace jit {
namespace backend {
namespace x64 {

extern const Register x64_registers[];
extern const int x64_num_registers;
extern const int x64_arg0_idx;
extern const int x64_arg1_idx;
extern const int x64_arg2_idx;
extern const int x64_tmp0_idx;
extern const int x64_tmp1_idx;

class X64Backend : public Backend {
 public:
  X64Backend(hw::Memory &memory);
  ~X64Backend();

  const Register *registers() const;
  int num_registers() const;

  void Reset();

  BlockPointer AssembleBlock(ir::IRBuilder &builder, void *guest_ctx,
                             int block_flags);

  bool HandleFastmemException(sys::Exception &ex);

 private:
  X64Emitter emitter_;
};
}
}
}
}

#endif
