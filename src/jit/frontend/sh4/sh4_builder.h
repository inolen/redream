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
  SH4Builder(Arena &arena, hw::Memory &memory, const SH4Context &guest_ctx);

  void Emit(uint32_t addr, int max_instrs);

  ir::Value *LoadGPR(int n, ir::ValueType type);
  void StoreGPR(int n, ir::Value *v);
  ir::Value *LoadFPR(int n, ir::ValueType type);
  void StoreFPR(int n, ir::Value *v);
  ir::Value *LoadXFR(int n, ir::ValueType type);
  void StoreXFR(int n, ir::Value *v);
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

  void Branch(ir::Value *dest);
  void BranchCond(ir::Value *cond, ir::Value *true_addr, ir::Value *false_addr);

  void InvalidInstruction(uint32_t guest_addr);

  bool EmitDelayInstr(const Instr &prev);

 private:
  hw::Memory &memory_;
  const SH4Context &guest_ctx_;
  uint32_t pc_;
  int guest_cycles_;
  FPUState fpu_state_;
};
}
}
}
}

#endif
