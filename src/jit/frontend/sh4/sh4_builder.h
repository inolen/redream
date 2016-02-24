#ifndef SH4_BUILDER_H
#define SH4_BUILDER_H

#include "jit/frontend/sh4/sh4_context.h"
#include "jit/frontend/sh4/sh4_disassembler.h"
#include "jit/ir/ir_builder.h"

namespace re {

namespace hw {
class Memory;
}

namespace jit {
namespace frontend {
namespace sh4 {

struct FPUState {
  bool double_pr;
  bool double_sz;
};

class SH4Builder : public ir::IRBuilder {
 public:
  SH4Builder(hw::Memory &memory);

  void Emit(uint32_t addr, int max_instrs, const SH4Context &ctx);

  ir::Value *LoadRegister(int n, ir::ValueTy type);
  void StoreRegister(int n, ir::Value *v);
  ir::Value *LoadRegisterF(int n, ir::ValueTy type);
  void StoreRegisterF(int n, ir::Value *v);
  ir::Value *LoadRegisterXF(int n, ir::ValueTy type);
  void StoreRegisterXF(int n, ir::Value *v);
  ir::Value *LoadSR();
  void StoreSR(ir::Value *v);
  ir::Value *LoadT();
  void StoreT(ir::Value *v);
  ir::Value *LoadGBR();
  void StoreGBR(ir::Value *v);
  ir::Value *LoadFPSCR();
  void StoreFPSCR(ir::Value *v);
  ir::Value *LoadPR();
  void StorePR(ir::Value *v);

  void InvalidInstruction(uint32_t guest_addr);

  bool EmitDelayInstr(const Instr &prev);

 private:
  hw::Memory &memory_;
  uint32_t pc_;
  int guest_cycles_;
  FPUState fpu_state_;
};
}
}
}
}

#endif
