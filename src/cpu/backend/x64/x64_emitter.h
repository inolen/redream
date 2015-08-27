#ifndef X64_EMITTER_H
#define X64_EMITTER_H

#include <memory>
#include <xbyak/xbyak.h>
#include "core/arena.h"
#include "core/platform.h"
#include "cpu/runtime.h"
#include "emu/memory.h"

namespace dreavm {
namespace cpu {
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

typedef uint32_t (*X64Fn)(void *guest_ctx, emu::Memory *memory);

class X64Emitter {
 public:
  X64Emitter(emu::Memory &memory, Xbyak::CodeGenerator &codegen);

  Xbyak::Label &epilog_label() { return *epilog_label_; }

  X64Fn Emit(ir::IRBuilder &builder);

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

  emu::Memory &memory_;
  Xbyak::CodeGenerator &c_;
  core::Arena arena_;
  Xbyak::Label *epilog_label_;
};
}
}
}
}

#endif
