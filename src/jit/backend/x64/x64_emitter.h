#ifndef X64_EMITTER_H
#define X64_EMITTER_H

#include <memory>
#include <xbyak/xbyak.h>
#include "core/arena.h"
#include "core/platform.h"
#include "hw/memory.h"
#include "jit/runtime.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace x64 {

enum {
#ifdef PLATFORM_WINDOWS
  STACK_SHADOW_SPACE = 32,
#else
  STACK_SHADOW_SPACE = 0,
#endif
  STACK_OFFSET_GUEST_CONTEXT = STACK_SHADOW_SPACE,
  STACK_OFFSET_MEMORY = STACK_SHADOW_SPACE + 8,
  STACK_OFFSET_LOCALS = STACK_SHADOW_SPACE + 16,
  STACK_SIZE = STACK_OFFSET_LOCALS
};

typedef uint32_t (*X64Fn)(void *guest_ctx, hw::Memory *memory);

class X64Emitter {
 public:
  X64Emitter(hw::Memory &memory);

  Xbyak::Label &epilog_label() { return *epilog_label_; }

  void Reset();

  bool Emit(ir::IRBuilder &builder, X64Fn *fn);

  // helpers for the emitter callbacks
  const Xbyak::Operand &GetOperand(const ir::Value *v, int size = -1);
  const Xbyak::Reg &GetRegister(const ir::Value *v);
  const Xbyak::Xmm &GetXMMRegister(const ir::Value *v);
  const Xbyak::Operand &CopyOperand(const ir::Value *v,
                                    const Xbyak::Operand &to);

  bool CanEncodeAsImmediate(const ir::Value *v) const;
  void RestoreArg0();
  void RestoreArg1();
  void RestoreArgs();

 private:
  Xbyak::Label *AllocLabel();
  Xbyak::Address *AllocAddress(const Xbyak::Address &addr);

  hw::Memory &memory_;
  Xbyak::CodeGenerator c_;
  Arena arena_;
  Xbyak::Label *epilog_label_;
};
}
}
}
}

#endif
