#ifndef X64_EMITTER_H
#define X64_EMITTER_H

#include <memory>
#include <xbyak/xbyak.h>
#include "core/arena.h"
#include "core/platform.h"
#include "hw/memory.h"
#include "jit/runtime.h"

extern const Xbyak::Reg &int_arg0;
extern const Xbyak::Reg &int_arg1;
extern const Xbyak::Reg &int_arg2;

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
  STACK_OFFSET_LOCALS = STACK_SHADOW_SPACE + 8,
  STACK_SIZE = STACK_OFFSET_LOCALS
};

typedef uint32_t (*X64Fn)(hw::Memory *, void *);

class X64Emitter : public Xbyak::CodeGenerator {
 public:
  X64Emitter(hw::Memory &memory, size_t max_size);
  ~X64Emitter();

  Xbyak::Label &epilog_label() { return *epilog_label_; }

  void Reset();

  X64Fn Emit(ir::IRBuilder &builder, void *guest_ctx);

  // helpers for the emitter callbacks
  const Xbyak::Operand &GetOperand(const ir::Value *v, int size = -1);
  const Xbyak::Reg &GetRegister(const ir::Value *v);
  const Xbyak::Xmm &GetXMMRegister(const ir::Value *v);
  const Xbyak::Operand &CopyOperand(const ir::Value *v,
                                    const Xbyak::Operand &to);

  bool CanEncodeAsImmediate(const ir::Value *v) const;
  void RestoreArgs();

  // private:
  Xbyak::Label *AllocLabel();
  Xbyak::Address *AllocAddress(const Xbyak::Address &addr);

  void EmitProlog(ir::IRBuilder &builder, int *stack_size);
  void EmitBody(ir::IRBuilder &builder);
  void EmitEpilog(ir::IRBuilder &builder, int stack_size);

  hw::Memory &memory_;
  Arena arena_;
  Xbyak::Label *epilog_label_;
  void *guest_ctx_;
  int modified_marker_;
  int *modified_;
};
}
}
}
}

#endif
