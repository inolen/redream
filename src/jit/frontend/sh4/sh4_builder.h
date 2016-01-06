#ifndef SH4_BUILDER_H
#define SH4_BUILDER_H

#include "hw/memory.h"
#include "jit/frontend/sh4/sh4_context.h"
#include "jit/frontend/sh4/sh4_instr.h"
#include "jit/ir/ir_builder.h"

namespace dvm {
namespace jit {
namespace frontend {
namespace sh4 {

struct FPUState {
  bool double_precision;
  bool single_precision_pair;
};

class SH4Builder : public ir::IRBuilder {
 public:
  SH4Builder(hw::Memory &memory);

  void Emit(uint32_t start_addr, const SH4Context &ctx);

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

  void PreserveT();
  void PreservePR();
  void PreserveRegister(int n);
  ir::Value *LoadPreserved();

  void EmitDelayInstr();

 private:
  void StoreAndPreserveContext(size_t offset, ir::Value *v,
                               ir::InstrFlag flags = ir::IF_NONE);

  hw::Memory &memory_;
  FPUState fpu_state_;
  Instr delay_instr_;
  bool has_delay_instr_;
  size_t preserve_offset_;
  uint32_t preserve_mask_;
  bool offset_preserved_;
};
}
}
}
}

#endif
