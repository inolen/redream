#ifndef X64_EMITTER_H
#define X64_EMITTER_H

#include <xbyak/xbyak.h>
#include "core/arena.h"
#include "hw/memory.h"
#include "jit/source_map.h"

namespace dvm {
namespace jit {
namespace backend {
namespace x64 {

enum {
#if PLATFORM_WINDOWS
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
  X64Emitter(void *buffer, size_t buffer_size);
  ~X64Emitter();

  Xbyak::Label &epilog_label() { return *epilog_label_; }
  SourceMap &source_map() { return *source_map_; }
  hw::Memory &memory() { return *memory_; }
  int block_flags() { return block_flags_; }

  void Reset();

  BlockPointer Emit(ir::IRBuilder &builder, SourceMap &source_map,
                    hw::Memory &memory, void *guest_ctx, int block_flags);

  // helpers for the emitter callbacks
  const Xbyak::Reg GetRegister(const ir::Value *v);
  const Xbyak::Xmm GetXMMRegister(const ir::Value *v);
  void CopyOperand(const ir::Value *v, const Xbyak::Reg &to);

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
  int num_temps_;
};
}
}
}
}

#endif
