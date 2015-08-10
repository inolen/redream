#ifndef X64_EMITTER_H
#define X64_EMITTER_H

#include <memory>
#include <xbyak/xbyak.h>
#include "core/arena.h"
#include "cpu/backend/x64/x64_block.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace x64 {

enum {
  STACK_OFFSET_GUEST_CONTEXT = 0,
  STACK_OFFSET_MEMORY = 8,
  STACK_OFFSET_LOCALS = 16
};

class X64Emitter {
 public:
  X64Emitter(Xbyak::CodeGenerator &codegen);

  Xbyak::Label &epilog_label() { return *epilog_label_; }

  X64Fn Emit(ir::IRBuilder &builder);

  // helpers for the emitter callbacks
  const Xbyak::Operand &GetOperand(const ir::Value *v, int size = -1);
  const Xbyak::Reg &GetRegister(const ir::Value *v);
  const Xbyak::Reg &GetPreferedRegister(const ir::Value *v,
                                        const Xbyak::Operand &prefered);
  const Xbyak::Reg &GetResultRegister(const ir::Value *v);
  const Xbyak::Reg &GetTmpRegister(const ir::Value *v = nullptr, int size = -1);

  const Xbyak::Operand &GetXMMOperand(const ir::Value *v);
  const Xbyak::Xmm &GetXMMRegister(const ir::Value *v);
  const Xbyak::Xmm &GetPreferedXMMRegister(const ir::Value *v,
                                           const Xbyak::Operand &prefered);
  const Xbyak::Xmm &GetTmpXMMRegister(const ir::Value *v = nullptr);

  const Xbyak::Operand &CopyOperand(const Xbyak::Operand &from,
                                    const Xbyak::Operand &to);
  const Xbyak::Operand &CopyOperand(const ir::Value *v,
                                    const Xbyak::Operand &to);

  bool CanEncodeAsImmediate(const ir::Value *v) const;
  void RestoreParameters();

 private:
  Xbyak::Label *AllocLabel();
  Xbyak::Address *AllocAddress(const Xbyak::Address &addr);

  void ResetTmpRegisters();

  Xbyak::CodeGenerator &c_;
  core::Arena arena_;
  Xbyak::Label *epilog_label_;
  int tmp_reg_;
  int tmp_xmm_reg_;
};
}
}
}
}

#endif
