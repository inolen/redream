#ifndef X64_EMITTER_H
#define X64_EMITTER_H

#include <xbyak/xbyak.h>
#include "core/arena.h"
#include "core/platform.h"
#include "hw/memory.h"
#include "jit/source_map.h"

namespace dvm {
namespace jit {
namespace backend {
namespace x64 {

#ifdef PLATFORM_WINDOWS
#define INT_ARG0 RCX
#define INT_ARG1 RDX
#define INT_ARG2 R8
#else
#define INT_ARG0 RDI
#define INT_ARG1 RSI
#define INT_ARG2 RDX
#endif

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

class X64Emitter : public Xbyak::CodeGenerator {
 public:
  X64Emitter(size_t max_size);
  ~X64Emitter();

  Xbyak::Label &epilog_label() { return *epilog_label_; }
  SourceMap &source_map() { return *source_map_; }
  hw::Memory &memory() { return *memory_; }
  int block_flags() { return block_flags_; }

  void Reset();

  BlockPointer Emit(ir::IRBuilder &builder, SourceMap &source_map,
                    hw::Memory &memory, void *guest_ctx, int block_flags);

  // helpers for the emitter callbacks
  const Xbyak::Operand &GetOperand(const ir::Value *v, int size = -1);
  const Xbyak::Reg &GetRegister(const ir::Value *v);
  const Xbyak::Xmm &GetXMMRegister(const ir::Value *v);
  const Xbyak::Operand &CopyOperand(const ir::Value *v,
                                    const Xbyak::Operand &to);

  Xbyak::Label *AllocLabel();

  bool CanEncodeAsImmediate(const ir::Value *v) const;
  void RestoreArgs();

  // private:
  void EmitProlog(ir::IRBuilder &builder, int *stack_size);
  void EmitBody(ir::IRBuilder &builder);
  void EmitEpilog(ir::IRBuilder &builder, int stack_size);

  Arena arena_;
  SourceMap *source_map_;
  hw::Memory *memory_;
  void *guest_ctx_;
  int block_flags_;
  Xbyak::Label *epilog_label_;
  int modified_marker_;
  int *modified_;
};
}
}
}
}

#endif
