#include "core/assert.h"
#include "emu/profiler.h"
#include "hw/memory.h"
#include "jit/frontend/sh4/sh4_builder.h"

using namespace re;
using namespace re::hw;
using namespace re::jit;
using namespace re::jit::frontend;
using namespace re::jit::frontend::sh4;
using namespace re::jit::ir;

static uint32_t s_fsca_table[0x20000] = {
#include "jit/frontend/sh4/sh4_fsca.inc"
};

typedef void (*EmitCallback)(SH4Builder &b, const FPUState &,
                             const sh4::Instr &i, bool *endblock);

#define EMITTER(name)                                                          \
  void Emit_OP_##name(SH4Builder &b, const FPUState &fpu, const sh4::Instr &i, \
                      bool *endblock)

#define EMIT_DELAYED()        \
  if (!b.EmitDelayInstr(i)) { \
    *endblock = true;         \
    return;                   \
  }

#define SH4_INSTR(name, desc, instr_code, cycles, flags) static EMITTER(name);
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR

EmitCallback emit_callbacks[sh4::NUM_OPCODES] = {
#define SH4_INSTR(name, desc, instr_code, cycles, flags) &Emit_OP_##name,
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
};

SH4Builder::SH4Builder(Arena &arena, Memory &memory,
                       const SH4Context &guest_ctx)
    : IRBuilder(arena), memory_(memory), guest_ctx_(guest_ctx) {}

void SH4Builder::Emit(uint32_t start_addr, int max_instrs) {
  PROFILER_RUNTIME("SH4Builder::Emit");

  pc_ = start_addr;
  guest_cycles_ = 0;
  fpu_state_.double_pr = guest_ctx_.fpscr & PR;
  fpu_state_.double_sz = guest_ctx_.fpscr & SZ;

  // clamp block to max_instrs if non-zero
  for (int i = 0; !max_instrs || i < max_instrs; i++) {
    Instr instr;
    instr.addr = pc_;
    instr.opcode = memory_.R16(instr.addr);
    Disasm(&instr);

    if (!instr.type) {
      InvalidInstruction(instr.addr);
      break;
    }

    pc_ += 2;
    guest_cycles_ += instr.type->cycles;

    // emit the current instruction
    bool endblock = false;
    (emit_callbacks[instr.type->op])(*this, fpu_state_, instr, &endblock);

    // end block if delay instruction is invalid
    if (endblock) {
      break;
    }

    // stop emitting once a branch has been hit. in addition, if fpscr has
    // changed, stop emitting since the fpu state is invalidated. also, if
    // sr has changed, stop emitting as there are interrupts that possibly
    // need to be handled
    if (instr.type->flags &
        (OP_FLAG_BRANCH | OP_FLAG_SET_FPSCR | OP_FLAG_SET_SR)) {
      break;
    }
  }

  ir::Instr *tail_instr = instrs_.tail();

  // if the block was terminated before a branch instruction, emit a
  // fallthrough branch to the next pc
  if (tail_instr->op() != OP_STORE_CONTEXT ||
      tail_instr->arg0()->i32() != offsetof(SH4Context, pc)) {
    Branch(AllocConstant(pc_));
  }

  // emit block epilog
  current_instr_ = tail_instr->prev();

  // update remaining cycles
  Value *num_cycles = LoadContext(offsetof(SH4Context, num_cycles), VALUE_I32);
  num_cycles = Sub(num_cycles, AllocConstant(guest_cycles_));
  StoreContext(offsetof(SH4Context, num_cycles), num_cycles);

  // update num instructions
  int sh4_num_instrs = (pc_ - start_addr) >> 1;
  Value *num_instrs = LoadContext(offsetof(SH4Context, num_instrs), VALUE_I32);
  num_instrs = Add(num_instrs, AllocConstant(sh4_num_instrs));
  StoreContext(offsetof(SH4Context, num_instrs), num_instrs);
}

Value *SH4Builder::LoadGPR(int n, ValueType type) {
  return LoadContext(offsetof(SH4Context, r[n]), type);
}

void SH4Builder::StoreGPR(int n, Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  return StoreContext(offsetof(SH4Context, r[n]), v);
}

Value *SH4Builder::LoadFPR(int n, ValueType type) {
  // swizzle 32-bit loads, see notes in sh4_context.h
  if (SizeForType(type) == 4) {
    n ^= 1;
  }
  return LoadContext(offsetof(SH4Context, fr[n]), type);
}

void SH4Builder::StoreFPR(int n, Value *v) {
  if (SizeForType(v->type()) == 4) {
    n ^= 1;
  }
  return StoreContext(offsetof(SH4Context, fr[n]), v);
}

Value *SH4Builder::LoadXFR(int n, ValueType type) {
  if (SizeForType(type) == 4) {
    n ^= 1;
  }
  return LoadContext(offsetof(SH4Context, xf[n]), type);
}

void SH4Builder::StoreXFR(int n, Value *v) {
  if (SizeForType(v->type()) == 4) {
    n ^= 1;
  }
  return StoreContext(offsetof(SH4Context, xf[n]), v);
}


Value *SH4Builder::LoadSR() {
  return LoadContext(offsetof(SH4Context, sr), VALUE_I32);
}

void SH4Builder::StoreSR(Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);

  Value *sr_updated = LoadContext(offsetof(SH4Context, SRUpdated), VALUE_I64);
  Value *old_sr = LoadSR();
  StoreContext(offsetof(SH4Context, sr), v);
  CallExternal2(sr_updated, ZExt(old_sr, VALUE_I64));
}

ir::Value *SH4Builder::LoadT() { return And(LoadSR(), AllocConstant(T)); }

void SH4Builder::StoreT(ir::Value *v) {
  Value *sr = LoadSR();
  Value *sr_t = Or(sr, AllocConstant(T));
  Value *sr_not = And(sr, AllocConstant(~T));
  StoreSR(Select(v, sr_t, sr_not));
}

Value *SH4Builder::LoadGBR() {
  return LoadContext(offsetof(SH4Context, gbr), VALUE_I32);
}

void SH4Builder::StoreGBR(Value *v) {
  StoreContext(offsetof(SH4Context, gbr), v);
}

ir::Value *SH4Builder::LoadFPSCR() {
  ir::Value *v = LoadContext(offsetof(SH4Context, fpscr), VALUE_I32);
  v = And(v, AllocConstant(0x003fffff));
  return v;
}

void SH4Builder::StoreFPSCR(ir::Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  v = And(v, AllocConstant(0x003fffff));

  Value *fpscr_updated =
      LoadContext(offsetof(SH4Context, FPSCRUpdated), VALUE_I64);
  Value *old_fpscr = LoadFPSCR();
  StoreContext(offsetof(SH4Context, fpscr), v);
  CallExternal2(fpscr_updated, ZExt(old_fpscr, VALUE_I64));
}

ir::Value *SH4Builder::LoadPR() {
  return LoadContext(offsetof(SH4Context, pr), VALUE_I32);
}

void SH4Builder::StorePR(ir::Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  StoreContext(offsetof(SH4Context, pr), v);
}

void SH4Builder::Branch(Value *dest) {
  StoreContext(offsetof(SH4Context, pc), dest);
}

void SH4Builder::BranchCond(Value *cond, Value *true_addr, Value *false_addr) {
  Value *dest = Select(cond, true_addr, false_addr);
  StoreContext(offsetof(SH4Context, pc), dest);
}

void SH4Builder::InvalidInstruction(uint32_t guest_addr) {
  Value *invalid_instruction =
      LoadContext(offsetof(SH4Context, InvalidInstruction), VALUE_I64);
  CallExternal2(invalid_instruction,
                AllocConstant(static_cast<uint64_t>(guest_addr)));
}

bool SH4Builder::EmitDelayInstr(const sh4::Instr &prev) {
  CHECK(prev.type->flags & OP_FLAG_DELAYED);

  Instr delay;
  delay.addr = prev.addr + 2;
  delay.opcode = memory_.R16(delay.addr);
  Disasm(&delay);

  if (!delay.type) {
    InvalidInstruction(delay.addr);
    return false;
  }

  CHECK(!(delay.type->flags & OP_FLAG_DELAYED));

  pc_ += 2;
  guest_cycles_ += delay.type->cycles;

  bool endblock = false;
  (emit_callbacks[delay.type->op])(*this, fpu_state_, delay, &endblock);

  return true;
}

// MOV     #imm,Rn
EMITTER(MOVI) {
  Value *v = b.AllocConstant((uint32_t)(int32_t)(int8_t)i.imm);
  b.StoreGPR(i.Rn, v);
}

// MOV.W   @(disp,PC),Rn
EMITTER(MOVWLPC) {
  uint32_t addr = (i.disp * 2) + i.addr + 4;
  Value *v = b.LoadGuest(b.AllocConstant(addr), VALUE_I16);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV.L   @(disp,PC),Rn
EMITTER(MOVLLPC) {
  uint32_t addr = (i.disp * 4) + (i.addr & ~3) + 4;
  Value *v = b.LoadGuest(b.AllocConstant(addr), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV     Rm,Rn
EMITTER(MOV) {
  Value *v = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV.B   Rm,@Rn
EMITTER(MOVBS) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.LoadGPR(i.Rm, VALUE_I8);
  b.StoreGuest(addr, v);
}

// MOV.W   Rm,@Rn
EMITTER(MOVWS) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.LoadGPR(i.Rm, VALUE_I16);
  b.StoreGuest(addr, v);
}

// MOV.L   Rm,@Rn
EMITTER(MOVLS) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreGuest(addr, v);
}

// MOV.B   @Rm,Rn
EMITTER(MOVBL) {
  Value *v = b.LoadGuest(b.LoadGPR(i.Rm, VALUE_I32), VALUE_I8);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV.W   @Rm,Rn
EMITTER(MOVWL) {
  Value *v = b.LoadGuest(b.LoadGPR(i.Rm, VALUE_I32), VALUE_I16);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV.L   @Rm,Rn
EMITTER(MOVLL) {
  Value *v = b.LoadGuest(b.LoadGPR(i.Rm, VALUE_I32), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV.B   Rm,@-Rn
EMITTER(MOVBM) {
  // decrease Rn by 1
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  addr = b.Sub(addr, b.AllocConstant(1));
  b.StoreGPR(i.Rn, addr);

  // store Rm at (Rn)
  Value *v = b.LoadGPR(i.Rm, VALUE_I8);
  b.StoreGuest(addr, v);
}

// MOV.W   Rm,@-Rn
EMITTER(MOVWM) {
  // decrease Rn by 2
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  addr = b.Sub(addr, b.AllocConstant(2));
  b.StoreGPR(i.Rn, addr);

  // store Rm at (Rn)
  Value *v = b.LoadGPR(i.Rm, VALUE_I16);
  b.StoreGuest(addr, v);
}

// MOV.L   Rm,@-Rn
EMITTER(MOVLM) {
  // decrease Rn by 4
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  addr = b.Sub(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);

  // store Rm at (Rn)
  Value *v = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreGuest(addr, v);
}

// MOV.B   @Rm+,Rn
EMITTER(MOVBP) {
  // store (Rm) at Rn
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I8);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(i.Rn, v);

  // increase Rm by 1
  // FIXME if rm != rn???
  addr = b.Add(addr, b.AllocConstant(1));
  b.StoreGPR(i.Rm, addr);
}

// MOV.W   @Rm+,Rn
EMITTER(MOVWP) {
  // store (Rm) at Rn
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I16);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(i.Rn, v);

  // increase Rm by 2
  // FIXME if rm != rn???
  addr = b.Add(addr, b.AllocConstant(2));
  b.StoreGPR(i.Rm, addr);
}

// MOV.L   @Rm+,Rn
EMITTER(MOVLP) {
  // store (Rm) at Rn
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreGPR(i.Rn, v);

  // increase Rm by 2
  // FIXME if rm != rn???
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// MOV.B   R0,@(disp,Rn)
EMITTER(MOVBS0D) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp));
  Value *v = b.LoadGPR(0, VALUE_I8);
  b.StoreGuest(addr, v);
}

// MOV.W   R0,@(disp,Rn)
EMITTER(MOVWS0D) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp * 2));
  Value *v = b.LoadGPR(0, VALUE_I16);
  b.StoreGuest(addr, v);
}

// MOV.L Rm,@(disp,Rn)
EMITTER(MOVLSMD) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp * 4));
  Value *v = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreGuest(addr, v);
}

// MOV.B   @(disp,Rm),R0
EMITTER(MOVBLD0) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp));
  Value *v = b.LoadGuest(addr, VALUE_I8);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(0, v);
}

// MOV.W   @(disp,Rm),R0
EMITTER(MOVWLD0) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp * 2));
  Value *v = b.LoadGuest(addr, VALUE_I16);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(0, v);
}

// MOV.L   @(disp,Rm),Rn
EMITTER(MOVLLDN) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp * 4));
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV.B   Rm,@(R0,Rn)
EMITTER(MOVBS0) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGPR(i.Rn, VALUE_I32));
  Value *v = b.LoadGPR(i.Rm, VALUE_I8);
  b.StoreGuest(addr, v);
}

// MOV.W   Rm,@(R0,Rn)
EMITTER(MOVWS0) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGPR(i.Rn, VALUE_I32));
  Value *v = b.LoadGPR(i.Rm, VALUE_I16);
  b.StoreGuest(addr, v);
}

// MOV.L   Rm,@(R0,Rn)
EMITTER(MOVLS0) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGPR(i.Rn, VALUE_I32));
  Value *v = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreGuest(addr, v);
}

// MOV.B   @(R0,Rm),Rn
EMITTER(MOVBL0) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGPR(i.Rm, VALUE_I32));
  Value *v = b.SExt(b.LoadGuest(addr, VALUE_I8), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV.W   @(R0,Rm),Rn
EMITTER(MOVWL0) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGPR(i.Rm, VALUE_I32));
  Value *v = b.LoadGuest(addr, VALUE_I16);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV.L   @(R0,Rm),Rn
EMITTER(MOVLL0) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGPR(i.Rm, VALUE_I32));
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MOV.B   R0,@(disp,GBR)
EMITTER(MOVBS0G) {
  Value *addr = b.LoadGBR();
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp));
  Value *v = b.LoadGPR(0, VALUE_I8);
  b.StoreGuest(addr, v);
}

// MOV.W   R0,@(disp,GBR)
EMITTER(MOVWS0G) {
  Value *addr = b.LoadGBR();
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp * 2));
  Value *v = b.LoadGPR(0, VALUE_I16);
  b.StoreGuest(addr, v);
}

// MOV.L   R0,@(disp,GBR)
EMITTER(MOVLS0G) {
  Value *addr = b.LoadGBR();
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp * 4));
  Value *v = b.LoadGPR(0, VALUE_I32);
  b.StoreGuest(addr, v);
}

// MOV.B   @(disp,GBR),R0
EMITTER(MOVBLG0) {
  Value *addr = b.LoadGBR();
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp));
  Value *v = b.LoadGuest(addr, VALUE_I8);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(0, v);
}

// MOV.W   @(disp,GBR),R0
EMITTER(MOVWLG0) {
  Value *addr = b.LoadGBR();
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp * 2));
  Value *v = b.LoadGuest(addr, VALUE_I16);
  v = b.SExt(v, VALUE_I32);
  b.StoreGPR(0, v);
}

// MOV.L   @(disp,GBR),R0
EMITTER(MOVLLG0) {
  Value *addr = b.LoadGBR();
  addr = b.Add(addr, b.AllocConstant((uint32_t)i.disp * 4));
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreGPR(0, v);
}

// MOVA    (disp,PC),R0
EMITTER(MOVA) {
  uint32_t addr = (i.disp * 4) + (i.addr & ~3) + 4;
  b.StoreGPR(0, b.AllocConstant(addr));
}

// MOVT    Rn
EMITTER(MOVT) { b.StoreGPR(i.Rn, b.LoadT()); }

// SWAP.B  Rm,Rn
EMITTER(SWAPB) {
  const int nbits = 8;
  Value *v = b.LoadGPR(i.Rm, VALUE_I32);
  Value *mask = b.AllocConstant((1u << nbits) - 1);
  Value *tmp = b.And(b.Xor(v, b.LShr(v, nbits)), mask);
  Value *res = b.Xor(v, b.Or(tmp, b.Shl(tmp, nbits)));
  b.StoreGPR(i.Rn, res);
}

// SWAP.W  Rm,Rn
EMITTER(SWAPW) {
  const int nbits = 16;
  Value *v = b.LoadGPR(i.Rm, VALUE_I32);
  Value *mask = b.AllocConstant((1u << nbits) - 1);
  Value *tmp = b.And(b.Xor(v, b.LShr(v, nbits)), mask);
  Value *res = b.Xor(v, b.Or(tmp, b.Shl(tmp, nbits)));
  b.StoreGPR(i.Rn, res);
}

// XTRCT   Rm,Rn
EMITTER(XTRCT) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  rn = b.LShr(b.And(rn, b.AllocConstant(0xffff0000)), 16);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  rm = b.Shl(b.And(rm, b.AllocConstant(0xffff)), 16);
  b.StoreGPR(i.Rn, b.Or(rn, rm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 1100  1       -
// ADD     Rm,Rn
EMITTER(ADD) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Add(rn, rm);
  b.StoreGPR(i.Rn, v);
}

// code                 cycles  t-bit
// 0111 nnnn iiii iiii  1       -
// ADD     #imm,Rn
EMITTER(ADDI) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *imm = b.AllocConstant((uint32_t)(int32_t)(int8_t)i.imm);
  Value *v = b.Add(rn, imm);
  b.StoreGPR(i.Rn, v);
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 1110  1       carry
// ADDC    Rm,Rn
EMITTER(ADDC) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Add(rn, rm);
  v = b.Add(v, b.LoadT());
  b.StoreGPR(i.Rn, v);

  // compute carry flag, taken from Hacker's Delight
  Value *and_rnrm = b.And(rn, rm);
  Value *or_rnrm = b.Or(rn, rm);
  Value *not_v = b.Not(v);
  Value *carry = b.And(or_rnrm, not_v);
  carry = b.Or(and_rnrm, carry);
  b.StoreT(carry);
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 1111  1       overflow
// ADDV    Rm,Rn
EMITTER(ADDV) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Add(rn, rm);
  b.StoreGPR(i.Rn, v);

  // compute overflow flag, taken from Hacker's Delight
  Value *xor_vrn = b.Xor(v, rn);
  Value *xor_vrm = b.Xor(v, rm);
  Value *overflow = b.LShr(b.And(xor_vrn, xor_vrm), 31);
  b.StoreT(overflow);
}

// code                 cycles  t-bit
// 1000 1000 iiii iiii  1       comparison result
// CMP/EQ #imm,R0
EMITTER(CMPEQI) {
  Value *imm = b.AllocConstant((uint32_t)(int32_t)(int8_t)i.imm);
  Value *r0 = b.LoadGPR(0, VALUE_I32);
  b.StoreT(b.CmpEQ(r0, imm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0000  1       comparison result
// CMP/EQ  Rm,Rn
EMITTER(CMPEQ) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreT(b.CmpEQ(rn, rm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0010  1       comparison result
// CMP/HS  Rm,Rn
EMITTER(CMPHS) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreT(b.CmpUGE(rn, rm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0011  1       comparison result
// CMP/GE  Rm,Rn
EMITTER(CMPGE) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreT(b.CmpSGE(rn, rm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0110  1       comparison result
// CMP/HI  Rm,Rn
EMITTER(CMPHI) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreT(b.CmpUGT(rn, rm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0111  1       comparison result
// CMP/GT  Rm,Rn
EMITTER(CMPGT) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreT(b.CmpSGT(rn, rm));
}

// code                 cycles  t-bit
// 0100 nnnn 0001 0001  1       comparison result
// CMP/PZ  Rn
EMITTER(CMPPZ) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  b.StoreT(b.CmpSGE(rn, b.AllocConstant(0)));
}

// code                 cycles  t-bit
// 0100 nnnn 0001 0101  1       comparison result
// CMP/PL  Rn
EMITTER(CMPPL) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  b.StoreT(b.CmpSGT(rn, b.AllocConstant(0)));
}

// code                 cycles  t-bit
// 0010 nnnn mmmm 1100  1       comparison result
// CMP/STR  Rm,Rn
EMITTER(CMPSTR) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *diff = b.Xor(rn, rm);

  // if any diff is zero, the bytes match
  Value *b4_eq =
      b.CmpEQ(b.And(diff, b.AllocConstant(0xff000000)), b.AllocConstant(0));
  Value *b3_eq =
      b.CmpEQ(b.And(diff, b.AllocConstant(0x00ff0000)), b.AllocConstant(0));
  Value *b2_eq =
      b.CmpEQ(b.And(diff, b.AllocConstant(0x0000ff00)), b.AllocConstant(0));
  Value *b1_eq =
      b.CmpEQ(b.And(diff, b.AllocConstant(0x000000ff)), b.AllocConstant(0));

  b.StoreT(b.Or(b.Or(b.Or(b1_eq, b2_eq), b3_eq), b4_eq));
}

// code                 cycles  t-bit
// 0010 nnnn mmmm 0111  1       calculation result
// DIV0S   Rm,Rn
EMITTER(DIV0S) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *qm = b.Xor(rn, rm);

  // update Q == M flag
  b.StoreContext(offsetof(SH4Context, sr_qm), b.Not(qm));

  // msb of Q ^ M -> T
  b.StoreT(b.LShr(qm, 31));
}

// code                 cycles  t-bit
// 0000 0000 0001 1001  1       0
// DIV0U
EMITTER(DIV0U) {  //
  b.StoreContext(offsetof(SH4Context, sr_qm), b.AllocConstant(0x80000000));

  b.StoreSR(b.And(b.LoadSR(), b.AllocConstant(~T)));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0100  1       calculation result
// DIV1 Rm,Rn
EMITTER(DIV1) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);

  // if Q == M, r0 = ~Rm and C = 1; else, r0 = Rm and C = 0
  Value *qm = b.AShr(b.LoadContext(offsetof(SH4Context, sr_qm), VALUE_I32), 31);
  Value *r0 = b.Xor(rm, qm);
  Value *carry = b.LShr(qm, 31);

  // initialize output bit as (Q == M) ^ Rn
  qm = b.Xor(qm, rn);

  // shift Rn left by 1 and add T
  rn = b.Shl(rn, 1);
  rn = b.Or(rn, b.LoadT());

  // add or subtract Rm based on r0 and C
  Value *rd = b.Add(rn, r0);
  rd = b.Add(rd, carry);
  b.StoreGPR(i.Rn, rd);

  // if C is cleared, invert output bit
  Value *and_rnr0 = b.And(rn, r0);
  Value *or_rnr0 = b.Or(rn, r0);
  Value *not_rd = b.Not(rd);
  carry = b.And(or_rnr0, not_rd);
  carry = b.Or(and_rnr0, carry);
  carry = b.LShr(carry, 31);
  qm = b.Select(carry, qm, b.Not(qm));
  b.StoreContext(offsetof(SH4Context, sr_qm), qm);

  // set T to output bit (which happens to be Q == M)
  b.StoreT(b.LShr(qm, 31));
}

// DMULS.L Rm,Rn
EMITTER(DMULS) {
  Value *rn = b.SExt(b.LoadGPR(i.Rn, VALUE_I32), VALUE_I64);
  Value *rm = b.SExt(b.LoadGPR(i.Rm, VALUE_I32), VALUE_I64);

  Value *p = b.SMul(rm, rn);
  Value *low = b.Trunc(p, VALUE_I32);
  Value *high = b.Trunc(b.LShr(p, 32), VALUE_I32);

  b.StoreContext(offsetof(SH4Context, macl), low);
  b.StoreContext(offsetof(SH4Context, mach), high);
}

// DMULU.L Rm,Rn
EMITTER(DMULU) {
  Value *rn = b.ZExt(b.LoadGPR(i.Rn, VALUE_I32), VALUE_I64);
  Value *rm = b.ZExt(b.LoadGPR(i.Rm, VALUE_I32), VALUE_I64);

  Value *p = b.UMul(rm, rn);
  Value *low = b.Trunc(p, VALUE_I32);
  Value *high = b.Trunc(b.LShr(p, 32), VALUE_I32);

  b.StoreContext(offsetof(SH4Context, macl), low);
  b.StoreContext(offsetof(SH4Context, mach), high);
}

// DT      Rn
EMITTER(DT) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.Sub(rn, b.AllocConstant(1));
  b.StoreGPR(i.Rn, v);
  b.StoreT(b.CmpEQ(v, b.AllocConstant(0)));
}

// EXTS.B  Rm,Rn
EMITTER(EXTSB) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I8);
  Value *v = b.SExt(rm, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// EXTS.W  Rm,Rn
EMITTER(EXTSW) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I16);
  Value *v = b.SExt(rm, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// EXTU.B  Rm,Rn
EMITTER(EXTUB) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I8);
  Value *v = b.ZExt(rm, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// EXTU.W  Rm,Rn
EMITTER(EXTUW) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I16);
  Value *v = b.ZExt(rm, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// MAC.L   @Rm+,@Rn+
EMITTER(MACL) { LOG_FATAL("MACL not implemented"); }

// MAC.W   @Rm+,@Rn+
EMITTER(MACW) { LOG_FATAL("MACW not implemented"); }

// MUL.L   Rm,Rn
EMITTER(MULL) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.SMul(rn, rm);
  b.StoreContext(offsetof(SH4Context, macl), v);
}

// MULS    Rm,Rn
EMITTER(MULS) {
  Value *rn = b.SExt(b.LoadGPR(i.Rn, VALUE_I16), VALUE_I32);
  Value *rm = b.SExt(b.LoadGPR(i.Rm, VALUE_I16), VALUE_I32);
  Value *v = b.SMul(rn, rm);
  b.StoreContext(offsetof(SH4Context, macl), v);
}

// MULU    Rm,Rn
EMITTER(MULU) {
  Value *rn = b.ZExt(b.LoadGPR(i.Rn, VALUE_I16), VALUE_I32);
  Value *rm = b.ZExt(b.LoadGPR(i.Rm, VALUE_I16), VALUE_I32);
  Value *v = b.UMul(rn, rm);
  b.StoreContext(offsetof(SH4Context, macl), v);
}

// NEG     Rm,Rn
EMITTER(NEG) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Neg(rm);
  b.StoreGPR(i.Rn, v);
}

// NEGC    Rm,Rn
EMITTER(NEGC) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *t = b.LoadT();
  Value *v = b.Sub(b.Neg(rm), t);
  b.StoreGPR(i.Rn, v);
  Value *carry = b.Or(t, rm);
  b.StoreT(carry);
}

// SUB     Rm,Rn
EMITTER(SUB) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Sub(rn, rm);
  b.StoreGPR(i.Rn, v);
}

// SUBC    Rm,Rn
EMITTER(SUBC) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Sub(rn, rm);
  v = b.Sub(v, b.LoadT());
  b.StoreGPR(i.Rn, v);

  // compute carry flag, taken from Hacker's Delight
  Value *l = b.And(b.Not(rn), rm);
  Value *r = b.And(b.Or(b.Not(rn), rm), v);
  Value *carry = b.Or(l, r);
  b.StoreT(carry);
}

// SUBV    Rm,Rn
EMITTER(SUBV) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Sub(rn, rm);
  b.StoreGPR(i.Rn, v);

  // compute overflow flag, taken from Hacker's Delight
  Value *xor_rnrm = b.Xor(rn, rm);
  Value *xor_vrn = b.Xor(v, rn);
  Value *overflow = b.LShr(b.And(xor_rnrm, xor_vrn), 31);
  b.StoreT(overflow);
}

// code                 cycles  t-bit
// 0010 nnnn mmmm 1001  1       -
// AND     Rm,Rn
EMITTER(AND) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.And(rn, rm);
  b.StoreGPR(i.Rn, v);
}

// code                 cycles  t-bit
// 1100 1001 iiii iiii  1       -
// AND     #imm,R0
EMITTER(ANDI) {
  Value *r0 = b.LoadGPR(0, VALUE_I32);
  Value *imm = b.AllocConstant((uint32_t)i.imm);
  Value *v = b.And(r0, imm);
  b.StoreGPR(0, v);
}

// code                 cycles  t-bit
// 1100 1101 iiii iiii  1       -
// AND.B   #imm,@(R0,GBR)
EMITTER(ANDB) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGBR());
  Value *v = b.LoadGuest(addr, VALUE_I8);
  v = b.And(v, b.AllocConstant((uint8_t)i.imm));
  b.StoreGuest(addr, v);
}

// NOT     Rm,Rn
EMITTER(NOT) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Not(rm);
  b.StoreGPR(i.Rn, v);
}

// OR      Rm,Rn
EMITTER(OR) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Or(rn, rm);
  b.StoreGPR(i.Rn, v);
}

// OR      #imm,R0
EMITTER(ORI) {
  Value *r0 = b.LoadGPR(0, VALUE_I32);
  Value *imm = b.AllocConstant((uint32_t)i.imm);
  Value *v = b.Or(r0, imm);
  b.StoreGPR(0, v);
}

// OR.B    #imm,@(R0,GBR)
EMITTER(ORB) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGBR());
  Value *v = b.LoadGuest(addr, VALUE_I8);
  v = b.Or(v, b.AllocConstant((uint8_t)i.imm));
  b.StoreGuest(addr, v);
}

// TAS.B   @Rn
EMITTER(TAS) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I8);
  b.StoreGuest(addr, b.Or(v, b.AllocConstant((uint8_t)0x80)));
  b.StoreT(b.CmpEQ(v, b.AllocConstant((uint8_t)0)));
}

// TST     Rm,Rn
EMITTER(TST) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.And(rn, rm);
  b.StoreT(b.CmpEQ(v, b.AllocConstant(0)));
}

// TST     #imm,R0
EMITTER(TSTI) {
  Value *r0 = b.LoadGPR(0, VALUE_I32);
  Value *imm = b.AllocConstant((uint32_t)i.imm);
  Value *v = b.And(r0, imm);
  b.StoreT(b.CmpEQ(v, b.AllocConstant((uint32_t)0)));
}

// TST.B   #imm,@(R0,GBR)
EMITTER(TSTB) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGBR());
  Value *data = b.LoadGuest(addr, VALUE_I8);
  Value *imm = b.AllocConstant((uint8_t)i.imm);
  Value *v = b.And(data, imm);
  b.StoreT(b.CmpEQ(v, b.AllocConstant((uint8_t)0)));
}

// XOR     Rm,Rn
EMITTER(XOR) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.Xor(rn, rm);
  b.StoreGPR(i.Rn, v);
}

// XOR     #imm,R0
EMITTER(XORI) {
  Value *r0 = b.LoadGPR(0, VALUE_I32);
  Value *imm = b.AllocConstant((uint32_t)i.imm);
  Value *v = b.Xor(r0, imm);
  b.StoreGPR(0, v);
}

// XOR.B   #imm,@(R0,GBR)
EMITTER(XORB) {
  Value *addr = b.LoadGPR(0, VALUE_I32);
  addr = b.Add(addr, b.LoadGBR());
  Value *data = b.LoadGuest(addr, VALUE_I8);
  Value *imm = b.AllocConstant((uint8_t)i.imm);
  Value *v = b.Xor(data, imm);
  b.StoreGuest(addr, v);
}

// ROTL    Rn
EMITTER(ROTL) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rn_msb = b.And(b.LShr(rn, 31), b.AllocConstant(0x1));
  Value *v = b.Or(b.Shl(rn, 1), rn_msb);
  b.StoreGPR(i.Rn, v);
  b.StoreT(rn_msb);
}

// ROTR    Rn
EMITTER(ROTR) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rn_lsb = b.And(rn, b.AllocConstant(0x1));
  Value *v = b.Shl(rn_lsb, 31);
  v = b.Or(v, b.LShr(rn, 1));
  b.StoreGPR(i.Rn, v);
  b.StoreT(rn_lsb);
}

// ROTCL   Rn
EMITTER(ROTCL) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rn_msb = b.And(b.LShr(rn, 31), b.AllocConstant(0x1));
  Value *v = b.Shl(rn, 1);
  v = b.Or(v, b.LoadT());
  b.StoreGPR(i.Rn, v);
  b.StoreT(rn_msb);
}

// ROTCR   Rn
EMITTER(ROTCR) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rn_lsb = b.And(rn, b.AllocConstant(0x1));
  Value *v = b.Shl(b.LoadT(), 31);
  v = b.Or(v, b.LShr(rn, 1));
  b.StoreGPR(i.Rn, v);
  b.StoreT(rn_lsb);
}

// SHAD    Rm,Rn
EMITTER(SHAD) {
  // when Rm >= 0, Rn << Rm
  // when Rm < 0, Rn >> Rm
  // when shifting right > 32, Rn = (Rn >= 0 ? 0 : -1)
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.AShd(rn, rm);
  b.StoreGPR(i.Rn, v);
}

// SHAL    Rn      (same as SHLL)
EMITTER(SHAL) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rn_msb = b.And(b.LShr(rn, 31), b.AllocConstant(0x1));
  Value *v = b.Shl(rn, 1);
  b.StoreGPR(i.Rn, v);
  b.StoreT(rn_msb);
}

// SHAR    Rn
EMITTER(SHAR) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rn_lsb = b.And(rn, b.AllocConstant(0x1));
  Value *v = b.AShr(rn, 1);
  b.StoreGPR(i.Rn, v);
  b.StoreT(rn_lsb);
}

// SHLD    Rm,Rn
EMITTER(SHLD) {
  // when Rm >= 0, Rn << Rm
  // when Rm < 0, Rn >> Rm
  // when shifting right >= 32, Rn = 0
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LShd(rn, rm);
  b.StoreGPR(i.Rn, v);
}

// SHLL    Rn      (same as SHAL)
EMITTER(SHLL) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rn_msb = b.And(b.LShr(rn, 31), b.AllocConstant(0x1));
  Value *v = b.Shl(rn, 1);
  b.StoreGPR(i.Rn, v);
  b.StoreT(rn_msb);
}

// SHLR    Rn
EMITTER(SHLR) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *rn_lsb = b.And(rn, b.AllocConstant(0x1));
  Value *v = b.LShr(rn, 1);
  b.StoreGPR(i.Rn, v);
  b.StoreT(rn_lsb);
}

// SHLL2   Rn
EMITTER(SHLL2) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.Shl(rn, 2);
  b.StoreGPR(i.Rn, v);
}

// SHLR2   Rn
EMITTER(SHLR2) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.LShr(rn, 2);
  b.StoreGPR(i.Rn, v);
}

// SHLL8   Rn
EMITTER(SHLL8) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.Shl(rn, 8);
  b.StoreGPR(i.Rn, v);
}

// SHLR8   Rn
EMITTER(SHLR8) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.LShr(rn, 8);
  b.StoreGPR(i.Rn, v);
}

// SHLL16  Rn
EMITTER(SHLL16) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.Shl(rn, 16);
  b.StoreGPR(i.Rn, v);
}

// SHLR16  Rn
EMITTER(SHLR16) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  Value *v = b.LShr(rn, 16);
  b.StoreGPR(i.Rn, v);
}

// code                 cycles  t-bit
// 1000 1011 dddd dddd  3/1     -
// BF      disp
EMITTER(BF) {
  uint32_t dest_addr = ((int8_t)i.disp * 2) + i.addr + 4;
  Value *cond = b.LoadT();
  b.BranchCond(cond, b.AllocConstant(i.addr + 2), b.AllocConstant(dest_addr));
}

// code                 cycles  t-bit
// 1000 1111 dddd dddd  3/1     -
// BFS     disp
EMITTER(BFS) {
  Value *cond = b.LoadT();
  EMIT_DELAYED();
  uint32_t dest_addr = ((int8_t)i.disp * 2) + i.addr + 4;
  b.BranchCond(cond, b.AllocConstant(i.addr + 4), b.AllocConstant(dest_addr));
}

// code                 cycles  t-bit
// 1000 1001 dddd dddd  3/1     -
// BT      disp
EMITTER(BT) {
  uint32_t dest_addr = ((int8_t)i.disp * 2) + i.addr + 4;
  Value *cond = b.LoadT();
  b.BranchCond(cond, b.AllocConstant(dest_addr), b.AllocConstant(i.addr + 2));
}

// code                 cycles  t-bit
// 1000 1101 dddd dddd  2/1     -
// BTS     disp
EMITTER(BTS) {
  Value *cond = b.LoadT();
  EMIT_DELAYED();
  uint32_t dest_addr = ((int8_t)i.disp * 2) + i.addr + 4;
  b.BranchCond(cond, b.AllocConstant(dest_addr), b.AllocConstant(i.addr + 4));
}

// code                 cycles  t-bit
// 1010 dddd dddd dddd  2       -
// BRA     disp
EMITTER(BRA) {
  EMIT_DELAYED();
  int32_t disp = ((i.disp & 0xfff) << 20) >>
                 20;  // 12-bit displacement must be sign extended
  uint32_t dest_addr = (disp * 2) + i.addr + 4;
  b.Branch(b.AllocConstant(dest_addr));
}

// code                 cycles  t-bit
// 0000 mmmm 0010 0011  2       -
// BRAF    Rn
EMITTER(BRAF) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  EMIT_DELAYED();
  Value *dest_addr = b.Add(b.AllocConstant(i.addr + 4), rn);
  b.Branch(dest_addr);
}

// code                 cycles  t-bit
// 1011 dddd dddd dddd  2       -
// BSR     disp
EMITTER(BSR) {
  EMIT_DELAYED();
  int32_t disp = ((i.disp & 0xfff) << 20) >>
                 20;  // 12-bit displacement must be sign extended
  uint32_t ret_addr = i.addr + 4;
  uint32_t dest_addr = ret_addr + disp * 2;
  b.StorePR(b.AllocConstant(ret_addr));
  b.Branch(b.AllocConstant(dest_addr));
}

// code                 cycles  t-bit
// 0000 mmmm 0000 0011  2       -
// BSRF    Rn
EMITTER(BSRF) {
  Value *rn = b.LoadGPR(i.Rn, VALUE_I32);
  EMIT_DELAYED();
  Value *ret_addr = b.AllocConstant(i.addr + 4);
  Value *dest_addr = b.Add(rn, ret_addr);
  b.StorePR(ret_addr);
  b.Branch(dest_addr);
}

// JMP     @Rm
EMITTER(JMP) {
  Value *dest_addr = b.LoadGPR(i.Rn, VALUE_I32);
  EMIT_DELAYED();
  b.Branch(dest_addr);
}

// JSR     @Rn
EMITTER(JSR) {
  Value *dest_addr = b.LoadGPR(i.Rn, VALUE_I32);
  EMIT_DELAYED();
  Value *ret_addr = b.AllocConstant(i.addr + 4);
  b.StorePR(ret_addr);
  b.Branch(dest_addr);
}

// RTS
EMITTER(RTS) {
  Value *dest_addr = b.LoadPR();
  EMIT_DELAYED();
  b.Branch(dest_addr);
}

// code                 cycles  t-bit
// 0000 0000 0010 1000  1       -
// CLRMAC
EMITTER(CLRMAC) {
  b.StoreContext(offsetof(SH4Context, mach), b.AllocConstant(0));
  b.StoreContext(offsetof(SH4Context, macl), b.AllocConstant(0));
}

EMITTER(CLRS) {
  Value *sr = b.LoadSR();
  sr = b.And(sr, b.AllocConstant(~S));
  b.StoreSR(sr);
}

// code                 cycles  t-bit
// 0000 0000 0000 1000  1       -
// CLRT
EMITTER(CLRT) { b.StoreT(b.AllocConstant(0)); }

// LDC     Rm,SR
EMITTER(LDCSR) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreSR(rm);
}

// LDC     Rm,GBR
EMITTER(LDCGBR) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreGBR(rm);
}

// LDC     Rm,VBR
EMITTER(LDCVBR) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, vbr), rm);
}

// LDC     Rm,SSR
EMITTER(LDCSSR) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, ssr), rm);
}

// LDC     Rm,SPC
EMITTER(LDCSPC) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, spc), rm);
}

// LDC     Rm,DBR
EMITTER(LDCDBR) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, dbr), rm);
}

// LDC.L   Rm,Rn_BANK
EMITTER(LDCRBANK) {
  int reg = i.Rn & 0x7;
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, ralt) + reg * 4, rm);
}

// LDC.L   @Rm+,SR
EMITTER(LDCMSR) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreSR(v);
  // reload Rm, sr store could have swapped banks
  addr = b.LoadGPR(i.Rm, VALUE_I32);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// LDC.L   @Rm+,GBR
EMITTER(LDCMGBR) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreGBR(v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// LDC.L   @Rm+,VBR
EMITTER(LDCMVBR) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, vbr), v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// LDC.L   @Rm+,SSR
EMITTER(LDCMSSR) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, ssr), v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// LDC.L   @Rm+,SPC
EMITTER(LDCMSPC) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, spc), v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// LDC.L   @Rm+,DBR
EMITTER(LDCMDBR) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, dbr), v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// LDC.L   @Rm+,Rn_BANK
EMITTER(LDCMRBANK) {
  int reg = i.Rn & 0x7;
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreGPR(i.Rm, b.Add(addr, b.AllocConstant(4)));
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, ralt) + reg * 4, v);
}

// LDS     Rm,MACH
EMITTER(LDSMACH) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, mach), rm);
}

// LDS     Rm,MACL
EMITTER(LDSMACL) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, macl), rm);
}

// LDS     Rm,PR
EMITTER(LDSPR) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StorePR(rm);
}

// LDS.L   @Rm+,MACH
EMITTER(LDSMMACH) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, mach), v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// LDS.L   @Rm+,MACL
EMITTER(LDSMMACL) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, macl), v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// LDS.L   @Rm+,PR
EMITTER(LDSMPR) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StorePR(v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// MOVCA.L     R0,@Rn
EMITTER(MOVCAL) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  Value *r0 = b.LoadGPR(0, VALUE_I32);
  b.StoreGuest(addr, r0);
}

// NOP
EMITTER(NOP) {}

// OCBI
EMITTER(OCBI) {}

// OCBP
EMITTER(OCBP) {}

// OCBWB
EMITTER(OCBWB) {}

// PREF     @Rn
EMITTER(PREF) {
  Value *prefetch = b.LoadContext(offsetof(SH4Context, Prefetch), VALUE_I64);
  Value *addr = b.ZExt(b.LoadGPR(i.Rn, VALUE_I32), VALUE_I64);
  b.CallExternal2(prefetch, addr);
}

// RTE
EMITTER(RTE) {
  Value *spc = b.LoadContext(offsetof(SH4Context, spc), VALUE_I32);
  Value *ssr = b.LoadContext(offsetof(SH4Context, ssr), VALUE_I32);
  b.StoreSR(ssr);
  EMIT_DELAYED();
  b.Branch(spc);
}

// SETS
EMITTER(SETS) { b.StoreSR(b.Or(b.LoadSR(), b.AllocConstant(S))); }

// SETT
EMITTER(SETT) { b.StoreT(b.AllocConstant(1)); }

// SLEEP
EMITTER(SLEEP) { LOG_FATAL("SLEEP not implemented"); }

// STC     SR,Rn
EMITTER(STCSR) {
  Value *v = b.LoadSR();
  b.StoreGPR(i.Rn, v);
}

// STC     GBR,Rn
EMITTER(STCGBR) {
  Value *v = b.LoadGBR();
  b.StoreGPR(i.Rn, v);
}

// STC     VBR,Rn
EMITTER(STCVBR) {
  Value *v = b.LoadContext(offsetof(SH4Context, vbr), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// STC     SSR,Rn
EMITTER(STCSSR) {
  Value *v = b.LoadContext(offsetof(SH4Context, ssr), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// STC     SPC,Rn
EMITTER(STCSPC) {
  Value *v = b.LoadContext(offsetof(SH4Context, spc), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// STC     SGR,Rn
EMITTER(STCSGR) {
  Value *v = b.LoadContext(offsetof(SH4Context, sgr), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// STC     DBR,Rn
EMITTER(STCDBR) {
  Value *v = b.LoadContext(offsetof(SH4Context, dbr), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// STC     Rm_BANK,Rn
EMITTER(STCRBANK) {
  int reg = i.Rm & 0x7;
  Value *v = b.LoadContext(offsetof(SH4Context, ralt) + reg * 4, VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// STC.L   SR,@-Rn
EMITTER(STCMSR) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  Value *v = b.LoadSR();
  b.StoreGuest(addr, v);
}

// STC.L   GBR,@-Rn
EMITTER(STCMGBR) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  Value *v = b.LoadGBR();
  b.StoreGuest(addr, v);
}

// STC.L   VBR,@-Rn
EMITTER(STCMVBR) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, vbr), VALUE_I32);
  b.StoreGuest(addr, v);
}

// STC.L   SSR,@-Rn
EMITTER(STCMSSR) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, ssr), VALUE_I32);
  b.StoreGuest(addr, v);
}

// STC.L   SPC,@-Rn
EMITTER(STCMSPC) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, spc), VALUE_I32);
  b.StoreGuest(addr, v);
}

// STC.L   SGR,@-Rn
EMITTER(STCMSGR) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, sgr), VALUE_I32);
  b.StoreGuest(addr, v);
}

// STC.L   DBR,@-Rn
EMITTER(STCMDBR) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, dbr), VALUE_I32);
  b.StoreGuest(addr, v);
}

// STC.L   Rm_BANK,@-Rn
EMITTER(STCMRBANK) {
  int reg = i.Rm & 0x7;
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, ralt) + reg * 4, VALUE_I32);
  b.StoreGuest(addr, v);
}

// STS     MACH,Rn
EMITTER(STSMACH) {
  Value *v = b.LoadContext(offsetof(SH4Context, mach), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// STS     MACL,Rn
EMITTER(STSMACL) {
  Value *v = b.LoadContext(offsetof(SH4Context, macl), VALUE_I32);
  b.StoreGPR(i.Rn, v);
}

// STS     PR,Rn
EMITTER(STSPR) {
  Value *v = b.LoadPR();
  b.StoreGPR(i.Rn, v);
}

// STS.L   MACH,@-Rn
EMITTER(STSMMACH) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);

  Value *mach = b.LoadContext(offsetof(SH4Context, mach), VALUE_I32);
  b.StoreGuest(addr, mach);
}

// STS.L   MACL,@-Rn
EMITTER(STSMMACL) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);

  Value *macl = b.LoadContext(offsetof(SH4Context, macl), VALUE_I32);
  b.StoreGuest(addr, macl);
}

// STS.L   PR,@-Rn
EMITTER(STSMPR) {
  Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);

  Value *pr = b.LoadPR();
  b.StoreGuest(addr, pr);
}

// TRAPA   #imm
EMITTER(TRAPA) { LOG_FATAL("TRAPA not implemented"); }

// FLDI0  FRn 1111nnnn10001101
EMITTER(FLDI0) { b.StoreFPR(i.Rn, b.AllocConstant(0)); }

// FLDI1  FRn 1111nnnn10011101
EMITTER(FLDI1) { b.StoreFPR(i.Rn, b.AllocConstant(0x3F800000)); }

// FMOV    FRm,FRn 1111nnnnmmmm1100
// FMOV    DRm,DRn 1111nnn0mmm01100
// FMOV    XDm,DRn 1111nnn0mmm11100
// FMOV    DRm,XDn 1111nnn1mmm01100
// FMOV    XDm,XDn 1111nnn1mmm11100
EMITTER(FMOV) {
  if (fpu.double_sz) {
    if (i.Rm & 1) {
      Value *rm = b.LoadXFR(i.Rm & 0xe, VALUE_I64);
      if (i.Rn & 1) {
        b.StoreXFR(i.Rn & 0xe, rm);
      } else {
        b.StoreFPR(i.Rn, rm);
      }
    } else {
      Value *rm = b.LoadFPR(i.Rm, VALUE_I64);
      if (i.Rn & 1) {
        b.StoreXFR(i.Rn & 0xe, rm);
      } else {
        b.StoreFPR(i.Rn, rm);
      }
    }
  } else {
    b.StoreFPR(i.Rn, b.LoadFPR(i.Rm, VALUE_I32));
  }
}

// FMOV.S  @Rm,FRn 1111nnnnmmmm1000
// FMOV    @Rm,DRn 1111nnn0mmmm1000
// FMOV    @Rm,XDn 1111nnn1mmmm1000
EMITTER(FMOV_LOAD) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);

  if (fpu.double_sz) {
    Value *v_low = b.LoadGuest(addr, VALUE_I32);
    Value *v_high = b.LoadGuest(b.Add(addr, b.AllocConstant(4)), VALUE_I32);
    if (i.Rn & 1) {
      b.StoreXFR(i.Rn & 0xe, v_low);
      b.StoreXFR(i.Rn, v_high);
    } else {
      b.StoreFPR(i.Rn, v_low);
      b.StoreFPR(i.Rn | 0x1, v_high);
    }
  } else {
    b.StoreFPR(i.Rn, b.LoadGuest(addr, VALUE_I32));
  }
}

// FMOV.S  @(R0,Rm),FRn 1111nnnnmmmm0110
// FMOV    @(R0,Rm),DRn 1111nnn0mmmm0110
// FMOV    @(R0,Rm),XDn 1111nnn1mmmm0110
EMITTER(FMOV_INDEX_LOAD) {
  Value *addr = b.Add(b.LoadGPR(0, VALUE_I32), b.LoadGPR(i.Rm, VALUE_I32));

  if (fpu.double_sz) {
    Value *v_low = b.LoadGuest(addr, VALUE_I32);
    Value *v_high = b.LoadGuest(b.Add(addr, b.AllocConstant(4)), VALUE_I32);
    if (i.Rn & 1) {
      b.StoreXFR(i.Rn & 0xe, v_low);
      b.StoreXFR(i.Rn, v_high);
    } else {
      b.StoreFPR(i.Rn, v_low);
      b.StoreFPR(i.Rn | 0x1, v_high);
    }
  } else {
    b.StoreFPR(i.Rn, b.LoadGuest(addr, VALUE_I32));
  }
}

// FMOV.S  FRm,@Rn 1111nnnnmmmm1010
// FMOV    DRm,@Rn 1111nnnnmmm01010
// FMOV    XDm,@Rn 1111nnnnmmm11010
EMITTER(FMOV_STORE) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);

  if (fpu.double_sz) {
    Value *addr_low = addr;
    Value *addr_high = b.Add(addr, b.AllocConstant(4));
    if (i.Rm & 1) {
      b.StoreGuest(addr_low, b.LoadXFR(i.Rm & 0xe, VALUE_I32));
      b.StoreGuest(addr_high, b.LoadXFR(i.Rm, VALUE_I32));
    } else {
      b.StoreGuest(addr_low, b.LoadFPR(i.Rm, VALUE_I32));
      b.StoreGuest(addr_high, b.LoadFPR(i.Rm | 0x1, VALUE_I32));
    }
  } else {
    b.StoreGuest(addr, b.LoadFPR(i.Rm, VALUE_I32));
  }
}

// FMOV.S  FRm,@(R0,Rn) 1111nnnnmmmm0111
// FMOV    DRm,@(R0,Rn) 1111nnnnmmm00111
// FMOV    XDm,@(R0,Rn) 1111nnnnmmm10111
EMITTER(FMOV_INDEX_STORE) {
  Value *addr = b.Add(b.LoadGPR(0, VALUE_I32), b.LoadGPR(i.Rn, VALUE_I32));

  if (fpu.double_sz) {
    Value *addr_low = addr;
    Value *addr_high = b.Add(addr, b.AllocConstant(4));
    if (i.Rm & 1) {
      b.StoreGuest(addr_low, b.LoadXFR(i.Rm & 0xe, VALUE_I32));
      b.StoreGuest(addr_high, b.LoadXFR(i.Rm, VALUE_I32));
    } else {
      b.StoreGuest(addr_low, b.LoadFPR(i.Rm, VALUE_I32));
      b.StoreGuest(addr_high, b.LoadFPR(i.Rm | 0x1, VALUE_I32));
    }
  } else {
    b.StoreGuest(addr, b.LoadFPR(i.Rm, VALUE_I32));
  }
}

// FMOV.S  FRm,@-Rn 1111nnnnmmmm1011
// FMOV    DRm,@-Rn 1111nnnnmmm01011
// FMOV    XDm,@-Rn 1111nnnnmmm11011
EMITTER(FMOV_SAVE) {
  if (fpu.double_sz) {
    Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(8));
    b.StoreGPR(i.Rn, addr);

    Value *addr_low = addr;
    Value *addr_high = b.Add(addr, b.AllocConstant(4));

    if (i.Rm & 1) {
      b.StoreGuest(addr_low, b.LoadXFR(i.Rm & 0xe, VALUE_I32));
      b.StoreGuest(addr_high, b.LoadXFR(i.Rm, VALUE_I32));
    } else {
      b.StoreGuest(addr_low, b.LoadFPR(i.Rm, VALUE_I32));
      b.StoreGuest(addr_high, b.LoadFPR(i.Rm | 0x1, VALUE_I32));
    }
  } else {
    Value *addr = b.Sub(b.LoadGPR(i.Rn, VALUE_I32), b.AllocConstant(4));
    b.StoreGPR(i.Rn, addr);
    b.StoreGuest(addr, b.LoadFPR(i.Rm, VALUE_I32));
  }
}

// FMOV.S  @Rm+,FRn 1111nnnnmmmm1001
// FMOV    @Rm+,DRn 1111nnn0mmmm1001
// FMOV    @Rm+,XDn 1111nnn1mmmm1001
EMITTER(FMOV_RESTORE) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);

  if (fpu.double_sz) {
    Value *v_low = b.LoadGuest(addr, VALUE_I32);
    Value *v_high = b.LoadGuest(b.Add(addr, b.AllocConstant(4)), VALUE_I32);
    if (i.Rn & 1) {
      b.StoreXFR(i.Rn & 0xe, v_low);
      b.StoreXFR(i.Rn, v_high);
    } else {
      b.StoreFPR(i.Rn, v_low);
      b.StoreFPR(i.Rn | 0x1, v_high);
    }
    b.StoreGPR(i.Rm, b.Add(addr, b.AllocConstant(8)));
  } else {
    b.StoreFPR(i.Rn, b.LoadGuest(addr, VALUE_I32));
    b.StoreGPR(i.Rm, b.Add(addr, b.AllocConstant(4)));
  }
}

// FLDS FRm,FPUL 1111mmmm00011101
EMITTER(FLDS) {
  Value *rn = b.LoadFPR(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, fpul), rn);
}

// FSTS FPUL,FRn 1111nnnn00001101
EMITTER(FSTS) {
  Value *fpul = b.LoadContext(offsetof(SH4Context, fpul), VALUE_I32);
  b.StoreFPR(i.Rn, fpul);
}

// FABS FRn PR=0 1111nnnn01011101
// FABS DRn PR=1 1111nnn001011101
EMITTER(FABS) {
  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    Value *v = b.FAbs(b.LoadFPR(n, VALUE_F64));
    b.StoreFPR(n, v);
  } else {
    Value *v = b.FAbs(b.LoadFPR(i.Rn, VALUE_F32));
    b.StoreFPR(i.Rn, v);
  }
}

// FSRRA FRn PR=0 1111nnnn01111101
EMITTER(FSRRA) {
  Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
  Value *v = b.FDiv(b.AllocConstant(1.0f), b.Sqrt(frn));
  b.StoreFPR(i.Rn, v);
}

// FADD FRm,FRn PR=0 1111nnnnmmmm0000
// FADD DRm,DRn PR=1 1111nnn0mmm00000
EMITTER(FADD) {
  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    Value *drn = b.LoadFPR(n, VALUE_F64);
    Value *drm = b.LoadFPR(m, VALUE_F64);
    Value *v = b.FAdd(drn, drm);
    b.StoreFPR(n, v);
  } else {
    Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
    Value *frm = b.LoadFPR(i.Rm, VALUE_F32);
    Value *v = b.FAdd(frn, frm);
    b.StoreFPR(i.Rn, v);
  }
}

// FCMP/EQ FRm,FRn PR=0 1111nnnnmmmm0100
// FCMP/EQ DRm,DRn PR=1 1111nnn0mmm00100
EMITTER(FCMPEQ) {
  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    Value *drn = b.LoadFPR(n, VALUE_F64);
    Value *drm = b.LoadFPR(m, VALUE_F64);
    Value *v = b.FCmpEQ(drn, drm);
    b.StoreT(v);
  } else {
    Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
    Value *frm = b.LoadFPR(i.Rm, VALUE_F32);
    Value *v = b.FCmpEQ(frn, frm);
    b.StoreT(v);
  }
}

// FCMP/GT FRm,FRn PR=0 1111nnnnmmmm0101
// FCMP/GT DRm,DRn PR=1 1111nnn0mmm00101
EMITTER(FCMPGT) {
  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    Value *drn = b.LoadFPR(n, VALUE_F64);
    Value *drm = b.LoadFPR(m, VALUE_F64);
    Value *v = b.FCmpGT(drn, drm);
    b.StoreT(v);
  } else {
    Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
    Value *frm = b.LoadFPR(i.Rm, VALUE_F32);
    Value *v = b.FCmpGT(frn, frm);
    b.StoreT(v);
  }
}

// FDIV FRm,FRn PR=0 1111nnnnmmmm0011
// FDIV DRm,DRn PR=1 1111nnn0mmm00011
EMITTER(FDIV) {
  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    Value *drn = b.LoadFPR(n, VALUE_F64);
    Value *drm = b.LoadFPR(m, VALUE_F64);
    Value *v = b.FDiv(drn, drm);
    b.StoreFPR(n, v);
  } else {
    Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
    Value *frm = b.LoadFPR(i.Rm, VALUE_F32);
    Value *v = b.FDiv(frn, frm);
    b.StoreFPR(i.Rn, v);
  }
}

// FLOAT FPUL,FRn PR=0 1111nnnn00101101
// FLOAT FPUL,DRn PR=1 1111nnn000101101
EMITTER(FLOAT) {
  Value *fpul = b.LoadContext(offsetof(SH4Context, fpul), VALUE_I32);

  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    Value *v = b.IToF(b.SExt(fpul, VALUE_I64), VALUE_F64);
    b.StoreFPR(n, v);
  } else {
    Value *v = b.IToF(fpul, VALUE_F32);
    b.StoreFPR(i.Rn, v);
  }
}

// FMAC FR0,FRm,FRn PR=0 1111nnnnmmmm1110
EMITTER(FMAC) {
  CHECK(!fpu.double_pr);

  Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
  Value *frm = b.LoadFPR(i.Rm, VALUE_F32);
  Value *fr0 = b.LoadFPR(0, VALUE_F32);
  Value *v = b.FAdd(b.FMul(fr0, frm), frn);
  b.StoreFPR(i.Rn, v);
}

// FMUL FRm,FRn PR=0 1111nnnnmmmm0010
// FMUL DRm,DRn PR=1 1111nnn0mmm00010
EMITTER(FMUL) {
  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    Value *drn = b.LoadFPR(n, VALUE_F64);
    Value *drm = b.LoadFPR(m, VALUE_F64);
    Value *v = b.FMul(drn, drm);
    b.StoreFPR(n, v);
  } else {
    Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
    Value *frm = b.LoadFPR(i.Rm, VALUE_F32);
    Value *v = b.FMul(frn, frm);
    b.StoreFPR(i.Rn, v);
  }
}

// FNEG FRn PR=0 1111nnnn01001101
// FNEG DRn PR=1 1111nnn001001101
EMITTER(FNEG) {
  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    Value *drn = b.LoadFPR(n, VALUE_F64);
    Value *v = b.FNeg(drn);
    b.StoreFPR(n, v);
  } else {
    Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
    Value *v = b.FNeg(frn);
    b.StoreFPR(i.Rn, v);
  }
}

// FSQRT FRn PR=0 1111nnnn01101101
// FSQRT DRn PR=1 1111nnnn01101101
EMITTER(FSQRT) {
  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    Value *drn = b.LoadFPR(n, VALUE_F64);
    Value *v = b.Sqrt(drn);
    b.StoreFPR(n, v);
  } else {
    Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
    Value *v = b.Sqrt(frn);
    b.StoreFPR(i.Rn, v);
  }
}

// FSUB FRm,FRn PR=0 1111nnnnmmmm0001
// FSUB DRm,DRn PR=1 1111nnn0mmm00001
EMITTER(FSUB) {
  if (fpu.double_pr) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    Value *drn = b.LoadFPR(n, VALUE_F64);
    Value *drm = b.LoadFPR(m, VALUE_F64);
    Value *v = b.FSub(drn, drm);
    b.StoreFPR(n, v);
  } else {
    Value *frn = b.LoadFPR(i.Rn, VALUE_F32);
    Value *frm = b.LoadFPR(i.Rm, VALUE_F32);
    Value *v = b.FSub(frn, frm);
    b.StoreFPR(i.Rn, v);
  }
}

// FTRC FRm,FPUL PR=0 1111mmmm00111101
// FTRC DRm,FPUL PR=1 1111mmm000111101
EMITTER(FTRC) {
  if (fpu.double_pr) {
    int m = i.Rm & 0xe;
    Value *drm = b.LoadFPR(m, VALUE_F64);
    Value *dpv = b.Trunc(b.FToI(drm, VALUE_I64), VALUE_I32);
    b.StoreContext(offsetof(SH4Context, fpul), dpv);
  } else {
    Value *frm = b.LoadFPR(i.Rm, VALUE_F32);
    Value *spv = b.FToI(frm, VALUE_I32);
    b.StoreContext(offsetof(SH4Context, fpul), spv);
  }
}

// FCNVDS DRm,FPUL PR=1 1111mmm010111101
EMITTER(FCNVDS) {
  CHECK(fpu.double_pr);

  // TODO rounding modes?

  int m = i.Rm & 0xe;
  Value *dpv = b.LoadFPR(m, VALUE_F64);
  Value *spv = b.FTrunc(dpv, VALUE_F32);
  b.StoreContext(offsetof(SH4Context, fpul), spv);
}

// FCNVSD FPUL, DRn PR=1 1111nnn010101101
EMITTER(FCNVSD) {
  CHECK(fpu.double_pr);

  // TODO rounding modes?

  Value *spv = b.LoadContext(offsetof(SH4Context, fpul), VALUE_F32);
  Value *dpv = b.FExt(spv, VALUE_F64);
  int n = i.Rn & 0xe;
  b.StoreFPR(n, dpv);
}

// LDS     Rm,FPSCR
EMITTER(LDSFPSCR) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreFPSCR(rm);
}

// LDS     Rm,FPUL
EMITTER(LDSFPUL) {
  Value *rm = b.LoadGPR(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, fpul), rm);
}

// LDS.L   @Rm+,FPSCR
EMITTER(LDSMFPSCR) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreFPSCR(v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// LDS.L   @Rm+,FPUL
EMITTER(LDSMFPUL) {
  Value *addr = b.LoadGPR(i.Rm, VALUE_I32);
  Value *v = b.LoadGuest(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, fpul), v);
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rm, addr);
}

// STS     FPSCR,Rn
EMITTER(STSFPSCR) {
  Value *fpscr = b.LoadFPSCR();
  b.StoreGPR(i.Rn, fpscr);
}

// STS     FPUL,Rn
EMITTER(STSFPUL) {
  Value *fpul = b.LoadContext(offsetof(SH4Context, fpul), VALUE_I32);
  b.StoreGPR(i.Rn, fpul);
}

// STS.L   FPSCR,@-Rn
EMITTER(STSMFPSCR) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  addr = b.Sub(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  b.StoreGuest(addr, b.LoadFPSCR());
}

// STS.L   FPUL,@-Rn
EMITTER(STSMFPUL) {
  Value *addr = b.LoadGPR(i.Rn, VALUE_I32);
  addr = b.Sub(addr, b.AllocConstant(4));
  b.StoreGPR(i.Rn, addr);
  Value *fpul = b.LoadContext(offsetof(SH4Context, fpul), VALUE_I32);
  b.StoreGuest(addr, fpul);
}

// FIPR FVm,FVn PR=0 1111nnmm11101101
EMITTER(FIPR) {
  int m = i.Rm << 2;
  int n = i.Rn << 2;

  Value *fvn = b.LoadFPR(n, VALUE_V128);
  Value *fvm = b.LoadFPR(m, VALUE_V128);
  Value *dp = b.VDot(fvn, fvm, VALUE_F32);
  b.StoreFPR(n + 3, dp);
}

// FSCA FPUL,DRn PR=0 1111nnn011111101
EMITTER(FSCA) {
  int n = i.Rn << 1;

  Value *fpul = b.LoadContext(offsetof(SH4Context, fpul), VALUE_I16);
  fpul = b.ZExt(fpul, VALUE_I64);

  Value *fsca_table = b.AllocConstant(reinterpret_cast<uint64_t>(s_fsca_table));
  Value *fsca_offset = b.Shl(fpul, 3);
  Value *addr = b.Add(fsca_table, fsca_offset);

  b.StoreFPR(n, b.LoadHost(addr, VALUE_F32));
  b.StoreFPR(n + 1,
             b.LoadHost(b.Add(addr, b.AllocConstant(INT64_C(4))), VALUE_F32));
}

// FTRV XMTRX,FVn PR=0 1111nn0111111101
EMITTER(FTRV) {
  int n = i.Rn << 2;

  // XF0 XF4 XF8  XF12     FR0     XF0 * FR0 + XF4 * FR1 + XF8  * FR2 + XF12 * FR3
  // XF1 XF5 XF9  XF13  *  FR1  =  XF1 * FR0 + XF5 * FR1 + XF9  * FR2 + XF13 * FR3
  // XF2 XF6 XF10 XF14     FR2     XF2 * FR0 + XF6 * FR1 + XF10 * FR2 + XF14 * FR3
  // XF3 XF7 XF11 XF15     FR3     XF3 * FR0 + XF7 * FR1 + XF11 * FR2 + XF15 * FR3

  Value *result = nullptr;

  Value *col0 = b.LoadXFR(0, VALUE_V128);
  Value *row0 = b.VBroadcast(b.LoadFPR(n + 0, VALUE_F32));
  result = b.VMul(col0, row0, VALUE_F32);

  Value *col1 = b.LoadXFR(4, VALUE_V128);
  Value *row1 = b.VBroadcast(b.LoadFPR(n + 1, VALUE_F32));
  result = b.VAdd(result, b.VMul(col1, row1, VALUE_F32), VALUE_F32);

  Value *col2 = b.LoadXFR(8, VALUE_V128);
  Value *row2 = b.VBroadcast(b.LoadFPR(n + 2, VALUE_F32));
  result = b.VAdd(result, b.VMul(col2, row2, VALUE_F32), VALUE_F32);

  Value *col3 = b.LoadXFR(12, VALUE_V128);
  Value *row3 = b.VBroadcast(b.LoadFPR(n + 3, VALUE_F32));
  result = b.VAdd(result, b.VMul(col3, row3, VALUE_F32), VALUE_F32);

  b.StoreFPR(n, result);
}

// FRCHG 1111101111111101
EMITTER(FRCHG) {
  Value *fpscr = b.LoadFPSCR();
  Value *v = b.Xor(fpscr, b.AllocConstant(FR));
  b.StoreFPSCR(v);
}

// FSCHG 1111001111111101
EMITTER(FSCHG) {
  Value *fpscr = b.LoadFPSCR();
  Value *v = b.Xor(fpscr, b.AllocConstant(SZ));
  b.StoreFPSCR(v);
}
