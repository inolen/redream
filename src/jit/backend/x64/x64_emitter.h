#ifndef X64_EMITTER_H
#define X64_EMITTER_H

#include <xbyak/xbyak.h>
#include "core/arena.h"

namespace re {

namespace hw {
class Memory;
}

namespace jit {
namespace backend {
namespace x64 {

enum {
#if PLATFORM_WINDOWS
  STACK_SHADOW_SPACE = 32,
#else
  STACK_SHADOW_SPACE = 0,
#endif
  STACK_OFFSET_LOCALS = STACK_SHADOW_SPACE,
  STACK_SIZE = STACK_OFFSET_LOCALS
};

enum XmmConstant {
  XMM_CONST_ABS_MASK_PS,
  XMM_CONST_ABS_MASK_PD,
  XMM_CONST_SIGN_MASK_PS,
  XMM_CONST_SIGN_MASK_PD,
  NUM_XMM_CONST,
};

class X64Emitter : public Xbyak::CodeGenerator {
 public:
  X64Emitter(void *buffer, size_t buffer_size);
  ~X64Emitter();

  void *guest_ctx() { return guest_ctx_; }
  hw::Memory *memory() { return memory_; }
  int block_flags() { return block_flags_; }

  void Reset();

  BlockPointer Emit(ir::IRBuilder &builder, hw::Memory &memory, void *guest_ctx,
                    int block_flags);

  // helpers for the emitter callbacks
  const Xbyak::Reg GetRegister(const ir::Value *v);
  const Xbyak::Xmm GetXmmRegister(const ir::Value *v);
  const Xbyak::Address GetXmmConstant(XmmConstant c);

  Xbyak::Label *AllocLabel();

  bool CanEncodeAsImmediate(const ir::Value *v) const;

 private:
  void EmitProlog(ir::IRBuilder &builder, int *stack_size);
  void EmitBody(ir::IRBuilder &builder);
  void EmitEpilog(ir::IRBuilder &builder, int stack_size);

  Arena arena_;
  hw::Memory *memory_;
  void *guest_ctx_;
  int block_flags_;
  int modified_marker_;
  int *modified_;
  int num_temps_;
  Xbyak::Label xmm_const_[NUM_XMM_CONST];
};
}
}
}
}

#endif
