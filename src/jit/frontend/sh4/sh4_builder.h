#ifndef SH4_BUILDER_H
#define SH4_BUILDER_H

#include "hw/memory.h"
#include "jit/frontend/sh4/sh4_context.h"
#include "jit/frontend/sh4/sh4_instr.h"
#include "jit/ir/ir_builder.h"

namespace dreavm {
namespace jit {
namespace frontend {
namespace sh4 {

// SR bits
enum {
  T = 0x00000001,   // true / false condition or carry/borrow bit
  S = 0x00000002,   // specifies a saturation operation for a MAC instruction
  I = 0x000000f0,   // interrupt mask level
  Q = 0x00000100,   // used by the DIV0S, DIV0U, and DIV1 instructions
  M = 0x00000200,   // used by the DIV0S, DIV0U, and DIV1 instructions
  FD = 0x00008000,  // an FPU instr causes a general FPU disable exception
  BL = 0x10000000,  // interrupt requests are masked
  RB = 0x20000000,  // general register bank specifier in privileged mode (set
                    // to 1 by a reset, exception, or interrupt)
  MD = 0x40000000   // processor mode (0 is user mode, 1 is privileged mode)
};

// FPSCR bits
enum {
  RM = 0x00000003,
  DN = 0x00040000,
  PR = 0x00080000,
  SZ = 0x00100000,
  FR = 0x00200000
};

struct FPUState {
  bool double_precision;
  bool single_precision_pair;
};

class SH4Builder : public ir::IRBuilder {
 public:
  SH4Builder(hw::Memory &memory);
  ~SH4Builder();

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
