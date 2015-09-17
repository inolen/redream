#define _USE_MATH_DEFINES
#include <math.h>
#include <stddef.h>
#include "jit/frontend/sh4/sh4_emit.h"
#include "jit/frontend/sh4/sh4_frontend.h"

using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::frontend;
using namespace dreavm::jit::frontend::sh4;
using namespace dreavm::jit::ir;

EmitCallback sh4::emit_callbacks[sh4::NUM_OPCODES] = {
#define SH4_INSTR(name, instr_code, cycles, flags) &sh4::Emit_OP_##name,
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
};

#define EMITTER(name)                                          \
  void sh4::Emit_OP_##name(SH4Builder &b, const FPUState &fpu, \
                           const sh4::Instr &i)

// MOV     #imm,Rn
EMITTER(MOVI) {
  Value *v = b.AllocConstant((uint32_t)(int32_t)(int8_t)i.imm);
  b.StoreRegister(i.Rn, v);
}

// MOV.W   @(disp,PC),Rn
EMITTER(MOVWLPC) {
  uint32_t addr = (i.disp * 2) + i.addr + 4;
  Value *v = b.SExt(b.Load(b.AllocConstant(addr), VALUE_I16), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV.L   @(disp,PC),Rn
EMITTER(MOVLLPC) {
  uint32_t addr = (i.disp * 4) + (i.addr & ~3) + 4;
  Value *v = b.Load(b.AllocConstant(addr), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV     Rm,Rn
EMITTER(MOV) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV.B   Rm,@Rn
EMITTER(MOVBS) {
  Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
  Value *v = b.LoadRegister(i.Rm, VALUE_I8);
  b.Store(addr, v);
}

// MOV.W   Rm,@Rn
EMITTER(MOVWS) {
  Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
  Value *v = b.LoadRegister(i.Rm, VALUE_I16);
  b.Store(addr, v);
}

// MOV.L   Rm,@Rn
EMITTER(MOVLS) {
  Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.Store(addr, v);
}

// MOV.B   @Rm,Rn
EMITTER(MOVBL) {
  Value *v =
      b.SExt(b.Load(b.LoadRegister(i.Rm, VALUE_I32), VALUE_I8), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV.W   @Rm,Rn
EMITTER(MOVWL) {
  Value *v =
      b.SExt(b.Load(b.LoadRegister(i.Rm, VALUE_I32), VALUE_I16), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV.L   @Rm,Rn
EMITTER(MOVLL) {
  Value *v = b.Load(b.LoadRegister(i.Rm, VALUE_I32), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV.B   Rm,@-Rn
EMITTER(MOVBM) {
  // decrease Rn by 1
  Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
  addr = b.Sub(addr, b.AllocConstant(1));
  b.StoreRegister(i.Rn, addr);

  // store Rm at (Rn)
  Value *v = b.LoadRegister(i.Rm, VALUE_I8);
  b.Store(addr, v);
}

// MOV.W   Rm,@-Rn
EMITTER(MOVWM) {
  // decrease Rn by 2
  Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
  addr = b.Sub(addr, b.AllocConstant(2));
  b.StoreRegister(i.Rn, addr);

  // store Rm at (Rn)
  Value *v = b.LoadRegister(i.Rm, VALUE_I16);
  b.Store(addr, v);
}

// MOV.L   Rm,@-Rn
EMITTER(MOVLM) {
  // decrease Rn by 4
  Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
  addr = b.Sub(addr, b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);

  // store Rm at (Rn)
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.Store(addr, v);
}

// MOV.B   @Rm+,Rn
EMITTER(MOVBP) {
  // store (Rm) at Rn
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.SExt(b.Load(addr, VALUE_I8), VALUE_I32);
  b.StoreRegister(i.Rn, v);

  // increase Rm by 1
  // FIXME if rm != rn???
  addr = b.Add(addr, b.AllocConstant(1));
  b.StoreRegister(i.Rm, addr);
}

// MOV.W   @Rm+,Rn
EMITTER(MOVWP) {
  // store (Rm) at Rn
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.SExt(b.Load(addr, VALUE_I16), VALUE_I32);
  b.StoreRegister(i.Rn, v);

  // increase Rm by 2
  // FIXME if rm != rn???
  addr = b.Add(addr, b.AllocConstant(2));
  b.StoreRegister(i.Rm, addr);
}

// MOV.L   @Rm+,Rn
EMITTER(MOVLP) {
  // store (Rm) at Rn
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreRegister(i.Rn, v);

  // increase Rm by 2
  // FIXME if rm != rn???
  addr = b.Add(addr, b.AllocConstant(4));
  b.StoreRegister(i.Rm, addr);
}

// MOV.B   R0,@(disp,Rn)
EMITTER(MOVBS0D) {
  Value *addr =
      b.Add(b.AllocConstant((uint32_t)i.disp), b.LoadRegister(i.Rn, VALUE_I32));
  Value *v = b.LoadRegister(0, VALUE_I8);
  b.Store(addr, v);
}

// MOV.W   R0,@(disp,Rn)
EMITTER(MOVWS0D) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp * 2),
                      b.LoadRegister(i.Rn, VALUE_I32));
  Value *v = b.LoadRegister(0, VALUE_I16);
  b.Store(addr, v);
}

// MOV.L Rm,@(disp,Rn)
EMITTER(MOVLSMD) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp * 4),
                      b.LoadRegister(i.Rn, VALUE_I32));
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.Store(addr, v);
}

// MOV.B   @(disp,Rm),R0
EMITTER(MOVBLD0) {
  Value *addr =
      b.Add(b.AllocConstant((uint32_t)i.disp), b.LoadRegister(i.Rm, VALUE_I32));
  Value *v = b.SExt(b.Load(addr, VALUE_I8), VALUE_I32);
  b.StoreRegister(0, v);
}

// MOV.W   @(disp,Rm),R0
EMITTER(MOVWLD0) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp * 2),
                      b.LoadRegister(i.Rm, VALUE_I32));
  Value *v = b.SExt(b.Load(addr, VALUE_I16), VALUE_I32);
  b.StoreRegister(0, v);
}

// MOV.L   @(disp,Rm),Rn
EMITTER(MOVLLDN) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp * 4),
                      b.LoadRegister(i.Rm, VALUE_I32));
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV.B   Rm,@(R0,Rn)
EMITTER(MOVBS0) {
  Value *addr =
      b.Add(b.LoadRegister(0, VALUE_I32), b.LoadRegister(i.Rn, VALUE_I32));
  Value *v = b.LoadRegister(i.Rm, VALUE_I8);
  b.Store(addr, v);
}

// MOV.W   Rm,@(R0,Rn)
EMITTER(MOVWS0) {
  Value *addr =
      b.Add(b.LoadRegister(0, VALUE_I32), b.LoadRegister(i.Rn, VALUE_I32));
  Value *v = b.LoadRegister(i.Rm, VALUE_I16);
  b.Store(addr, v);
}

// MOV.L   Rm,@(R0,Rn)
EMITTER(MOVLS0) {
  Value *addr =
      b.Add(b.LoadRegister(0, VALUE_I32), b.LoadRegister(i.Rn, VALUE_I32));
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.Store(addr, v);
}

// MOV.B   @(R0,Rm),Rn
EMITTER(MOVBL0) {
  Value *addr =
      b.Add(b.LoadRegister(0, VALUE_I32), b.LoadRegister(i.Rm, VALUE_I32));
  Value *v = b.SExt(b.Load(addr, VALUE_I8), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV.W   @(R0,Rm),Rn
EMITTER(MOVWL0) {
  Value *addr =
      b.Add(b.LoadRegister(0, VALUE_I32), b.LoadRegister(i.Rm, VALUE_I32));
  Value *v = b.SExt(b.Load(addr, VALUE_I16), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV.L   @(R0,Rm),Rn
EMITTER(MOVLL0) {
  Value *addr =
      b.Add(b.LoadRegister(0, VALUE_I32), b.LoadRegister(i.Rm, VALUE_I32));
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// MOV.B   R0,@(disp,GBR)
EMITTER(MOVBS0G) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp), b.LoadGBR());
  Value *v = b.LoadRegister(0, VALUE_I8);
  b.Store(addr, v);
}

// MOV.W   R0,@(disp,GBR)
EMITTER(MOVWS0G) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp * 2), b.LoadGBR());
  Value *v = b.LoadRegister(0, VALUE_I16);
  b.Store(addr, v);
}

// MOV.L   R0,@(disp,GBR)
EMITTER(MOVLS0G) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp * 4), b.LoadGBR());
  Value *v = b.LoadRegister(0, VALUE_I32);
  b.Store(addr, v);
}

// MOV.B   @(disp,GBR),R0
EMITTER(MOVBLG0) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp), b.LoadGBR());
  Value *v = b.SExt(b.Load(addr, VALUE_I8), VALUE_I32);
  b.StoreRegister(0, v);
}

// MOV.W   @(disp,GBR),R0
EMITTER(MOVWLG0) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp * 2), b.LoadGBR());
  Value *v = b.SExt(b.Load(addr, VALUE_I16), VALUE_I32);
  b.StoreRegister(0, v);
}

// MOV.L   @(disp,GBR),R0
EMITTER(MOVLLG0) {
  Value *addr = b.Add(b.AllocConstant((uint32_t)i.disp * 4), b.LoadGBR());
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreRegister(0, v);
}

// MOVA    @(disp,PC),R0
EMITTER(MOVA) {
  Value *addr = b.AllocConstant((uint32_t)((i.disp * 4) + (i.addr & ~3) + 4));
  b.StoreRegister(0, addr);
}

// MOVT    Rn
EMITTER(MOVT) { b.StoreRegister(i.Rn, b.LoadT()); }

// SWAP.B  Rm,Rn
EMITTER(SWAPB) {
  const int nbits = 8;
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  Value *tmp =
      b.And(b.Xor(v, b.LShr(v, nbits)), b.AllocConstant((1u << nbits) - 1));
  Value *res = b.Xor(v, b.Or(tmp, b.Shl(tmp, nbits)));
  b.StoreRegister(i.Rn, res);
}

// SWAP.W  Rm,Rn
EMITTER(SWAPW) {
  const int nbits = 16;
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  Value *tmp =
      b.And(b.Xor(v, b.LShr(v, nbits)), b.AllocConstant((1u << nbits) - 1));
  Value *res = b.Xor(v, b.Or(tmp, b.Shl(tmp, nbits)));
  b.StoreRegister(i.Rn, res);
}

// XTRCT   Rm,Rn
EMITTER(XTRCT) {
  Value *Rm = b.Shl(
      b.And(b.LoadRegister(i.Rm, VALUE_I32), b.AllocConstant(0xffff)), 16);
  Value *Rn = b.LShr(
      b.And(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(0xffff0000)), 16);
  b.StoreRegister(i.Rn, b.Or(Rm, Rn));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 1100  1       -
// ADD     Rm,Rn
EMITTER(ADD) {
  Value *v =
      b.Add(b.LoadRegister(i.Rn, VALUE_I32), b.LoadRegister(i.Rm, VALUE_I32));
  b.StoreRegister(i.Rn, v);
}

// code                 cycles  t-bit
// 0111 nnnn iiii iiii  1       -
// ADD     #imm,Rn
EMITTER(ADDI) {
  Value *v = b.Add(b.LoadRegister(i.Rn, VALUE_I32),
                   b.AllocConstant((uint32_t)(int32_t)(int8_t)i.imm));
  b.StoreRegister(i.Rn, v);
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 1110  1       carry
// ADDC    Rm,Rn
EMITTER(ADDC) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *v1 = b.Add(rn, rm);
  Value *v2 = b.Add(v1, b.LoadT());
  b.StoreRegister(i.Rn, v2);

  // if the available bits were overflowed, set the carry flag
  Value *rnrm_overflow = b.UGT(rn, v1);
  Value *rnrmt_overflow = b.UGT(v1, v2);
  Value *overflow = b.Or(rnrm_overflow, rnrmt_overflow);
  b.StoreT(overflow);
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 1111  1       overflow
// ADDV    Rm,Rn
EMITTER(ADDV) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *v = b.Add(rn, rm);
  b.StoreRegister(i.Rn, v);

  // if Rm and Rn are the same sign, but value is different, overflowed
  Value *rm_ge_0 = b.SGE(rm, b.AllocConstant(0));
  Value *rn_ge_0 = b.SGE(rn, b.AllocConstant(0));
  Value *v_ge_0 = b.SGE(v, b.AllocConstant(0));
  Value *overflow = b.And(b.EQ(rn_ge_0, rm_ge_0), b.NE(rm_ge_0, v_ge_0));
  b.StoreT(overflow);
}

// code                 cycles  t-bit
// 1000 1000 iiii iiii  1       comparison result
// CMP/EQ #imm,R0
EMITTER(CMPEQI) {
  Value *imm = b.AllocConstant((uint32_t)(int32_t)(int8_t)i.imm);
  Value *r0 = b.LoadRegister(0, VALUE_I32);
  b.StoreT(b.EQ(r0, imm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0000  1       comparison result
// CMP/EQ  Rm,Rn
EMITTER(CMPEQ) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreT(b.EQ(rn, rm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0010  1       comparison result
// CMP/HS  Rm,Rn
EMITTER(CMPHS) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreT(b.UGE(rn, rm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0011  1       comparison result
// CMP/GE  Rm,Rn
EMITTER(CMPGE) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreT(b.SGE(rn, rm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0110  1       comparison result
// CMP/HI  Rm,Rn
EMITTER(CMPHI) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreT(b.UGT(rn, rm));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0111  1       comparison result
// CMP/GT  Rm,Rn
EMITTER(CMPGT) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreT(b.SGT(rn, rm));
}

// code                 cycles  t-bit
// 0100 nnnn 0001 0001  1       comparison result
// CMP/PZ  Rn
EMITTER(CMPPZ) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreT(b.SGE(rn, b.AllocConstant(0)));
}

// code                 cycles  t-bit
// 0100 nnnn 0001 0101  1       comparison result
// CMP/PL  Rn
EMITTER(CMPPL) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreT(b.SGT(rn, b.AllocConstant(0)));
}

// code                 cycles  t-bit
// 0010 nnnn mmmm 1100  1       comparison result
// CMP/STR  Rm,Rn
EMITTER(CMPSTR) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *diff = b.Xor(rn, rm);

  // if any diff is zero, the bytes match
  Value *b4_eq =
      b.EQ(b.And(diff, b.AllocConstant(0xff000000)), b.AllocConstant(0));
  Value *b3_eq =
      b.EQ(b.And(diff, b.AllocConstant(0x00ff0000)), b.AllocConstant(0));
  Value *b2_eq =
      b.EQ(b.And(diff, b.AllocConstant(0x0000ff00)), b.AllocConstant(0));
  Value *b1_eq =
      b.EQ(b.And(diff, b.AllocConstant(0x000000ff)), b.AllocConstant(0));

  b.StoreT(b.Or(b.Or(b.Or(b1_eq, b2_eq), b3_eq), b4_eq));
}

// code                 cycles  t-bit
// 0010 nnnn mmmm 0111  1       calculation result
// DIV0S   Rm,Rn
EMITTER(DIV0S) {
  Value *sr = b.LoadSR();
  Value *rm_msb =
      b.And(b.LoadRegister(i.Rm, VALUE_I32), b.AllocConstant(0x80000000));
  Value *rn_msb =
      b.And(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(0x80000000));
  // MSB of Rn -> Q
  sr = b.Select(b.NE(rn_msb, b.AllocConstant(0)), b.Or(sr, b.AllocConstant(Q)),
                b.And(sr, b.AllocConstant(~Q)));
  // MSB of Rm -> M
  sr = b.Select(b.NE(rm_msb, b.AllocConstant(0)), b.Or(sr, b.AllocConstant(M)),
                b.And(sr, b.AllocConstant(~M)));
  // M ^ Q -> T
  sr = b.Select(b.NE(b.Xor(rm_msb, rn_msb), b.AllocConstant(0)),
                b.Or(sr, b.AllocConstant(T)), b.And(sr, b.AllocConstant(~T)));
  b.StoreSR(sr);
}

// code                 cycles  t-bit
// 0000 0000 0001 1001  1       0
// DIV0U
EMITTER(DIV0U) {  //
  b.StoreSR(b.And(b.LoadSR(), b.AllocConstant(~(Q | M | T))));
}

// code                 cycles  t-bit
// 0011 nnnn mmmm 0100  1       calculation result
// DIV1 Rm,Rn
EMITTER(DIV1) {
  Block *noq_block = b.AppendBlock();
  Block *noq_negative_block = b.AppendBlock();
  Block *noq_nonnegative_block = b.AppendBlock();
  Block *q_block = b.AppendBlock();
  Block *q_negative_block = b.AppendBlock();
  Block *q_nonnegative_block = b.AppendBlock();
  Block *end_block = b.AppendBlock();

  // save off q, and update it based on Rn
  Value *sr = b.LoadSR();
  Value *dividend = b.LoadRegister(i.Rn, VALUE_I32);
  Value *old_q = b.And(sr, b.AllocConstant(Q));
  Value *new_q = b.And(dividend, b.AllocConstant(0x80000000));
  b.StoreSR(b.Select(new_q, b.Or(sr, b.AllocConstant(Q)),
                     b.And(sr, b.AllocConstant(~Q))));

  b.BranchCond(old_q, q_block, noq_block);

  {
    b.SetCurrentBlock(q_block);
    Value *dividend_is_neg = b.And(b.LoadSR(), b.AllocConstant(M));
    b.BranchCond(dividend_is_neg, q_negative_block, q_nonnegative_block);

    {
      // M is set, Q is set
      b.SetCurrentBlock(q_negative_block);
      // rotate dividend left, moving T to the LSB
      Value *dividend =
          b.Or(b.Shl(b.LoadRegister(i.Rn, VALUE_I32), 1), b.LoadT());
      Value *divisor = b.LoadRegister(i.Rm, VALUE_I32);
      Value *new_dividend = b.Sub(dividend, divisor);
      b.StoreRegister(i.Rn, new_dividend);
      b.BranchTrue(b.UGT(new_dividend, dividend),
                   end_block);  // M is set, Q is set
      b.StoreSR(
          b.Xor(b.LoadSR(), b.AllocConstant(Q)));  // M is set, Q is not set
      b.Branch(end_block);
    }

    {
      // M is not set, Q is set
      b.SetCurrentBlock(q_nonnegative_block);
      // rotate dividend left, moving T to the LSB
      Value *dividend =
          b.Or(b.Shl(b.LoadRegister(i.Rn, VALUE_I32), 1), b.LoadT());
      Value *divisor = b.LoadRegister(i.Rm, VALUE_I32);
      Value *new_dividend = b.Add(dividend, divisor);
      b.StoreRegister(i.Rn, new_dividend);
      b.BranchTrue(b.UGE(new_dividend, dividend),
                   end_block);  // M is not set, Q is set
      b.StoreSR(
          b.Xor(b.LoadSR(), b.AllocConstant(Q)));  // M is not set, Q is not set
      b.Branch(end_block);
    }
  }

  {
    b.SetCurrentBlock(noq_block);
    Value *dividend_is_neg = b.And(b.LoadSR(), b.AllocConstant(M));
    b.BranchCond(dividend_is_neg, noq_negative_block, noq_nonnegative_block);

    {
      // M is set, Q is not set
      b.SetCurrentBlock(noq_negative_block);
      // rotate dividend left, moving T to the LSB
      Value *dividend =
          b.Or(b.Shl(b.LoadRegister(i.Rn, VALUE_I32), 1), b.LoadT());
      Value *divisor = b.LoadRegister(i.Rm, VALUE_I32);
      Value *new_dividend = b.Add(dividend, divisor);
      b.StoreRegister(i.Rn, new_dividend);
      b.BranchTrue(b.ULT(new_dividend, dividend),
                   end_block);  // M is set, Q is not set
      b.StoreSR(b.Xor(b.LoadSR(), b.AllocConstant(Q)));  // M is set, Q is set
      b.Branch(end_block);
    }

    {
      // M is not set, Q is not set
      b.SetCurrentBlock(noq_nonnegative_block);
      // rotate dividend left, moving T to the LSB
      Value *dividend =
          b.Or(b.Shl(b.LoadRegister(i.Rn, VALUE_I32), 1), b.LoadT());
      Value *divisor = b.LoadRegister(i.Rm, VALUE_I32);
      Value *new_dividend = b.Sub(dividend, divisor);
      b.StoreRegister(i.Rn, new_dividend);
      b.BranchTrue(b.ULE(new_dividend, dividend),
                   end_block);  // M is not set, Q is not set
      b.StoreSR(
          b.Xor(b.LoadSR(), b.AllocConstant(Q)));  // M is not set, Q is set
      b.Branch(end_block);
    }
  }

  {
    b.SetCurrentBlock(end_block);
    Value *sr = b.LoadSR();
    Value *dividend_is_neg = b.And(sr, b.AllocConstant(M));
    Value *new_q = b.And(sr, b.AllocConstant(Q));
    b.StoreT(b.EQ(b.EQ(dividend_is_neg, b.AllocConstant(0)),
                  b.EQ(new_q, b.AllocConstant(0))));
  }

  // b.SetCurrentBlock(end_block);
}

// DMULS.L Rm,Rn
EMITTER(DMULS) {
  Value *rm = b.SExt(b.LoadRegister(i.Rm, VALUE_I32), VALUE_I64);
  Value *rn = b.SExt(b.LoadRegister(i.Rn, VALUE_I32), VALUE_I64);

  Value *p = b.SMul(rm, rn);
  Value *low = b.Truncate(p, VALUE_I32);
  Value *high = b.Truncate(b.LShr(p, 32), VALUE_I32);

  b.StoreContext(offsetof(SH4Context, macl), low);
  b.StoreContext(offsetof(SH4Context, mach), high);
}

// DMULU.L Rm,Rn
EMITTER(DMULU) {
  Value *rm = b.ZExt(b.LoadRegister(i.Rm, VALUE_I32), VALUE_I64);
  Value *rn = b.ZExt(b.LoadRegister(i.Rn, VALUE_I32), VALUE_I64);

  Value *p = b.UMul(rm, rn);
  Value *low = b.Truncate(p, VALUE_I32);
  Value *high = b.Truncate(b.LShr(p, 32), VALUE_I32);

  b.StoreContext(offsetof(SH4Context, macl), low);
  b.StoreContext(offsetof(SH4Context, mach), high);
}

// DT      Rn
EMITTER(DT) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *v = b.Sub(rn, b.AllocConstant(1));
  b.StoreRegister(i.Rn, v);
  b.StoreT(b.EQ(v, b.AllocConstant(0)));
}

// EXTS.B  Rm,Rn
EMITTER(EXTSB) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I8);
  b.StoreRegister(i.Rn, b.SExt(rm, VALUE_I32));
}

// EXTS.W  Rm,Rn
EMITTER(EXTSW) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I16);
  b.StoreRegister(i.Rn, b.SExt(rm, VALUE_I32));
}

// EXTU.B  Rm,Rn
EMITTER(EXTUB) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I8);
  b.StoreRegister(i.Rn, b.ZExt(rm, VALUE_I32));
}

// EXTU.W  Rm,Rn
EMITTER(EXTUW) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I16);
  b.StoreRegister(i.Rn, b.ZExt(rm, VALUE_I32));
}

// MAC.L   @Rm+,@Rn+
EMITTER(MACL) { LOG_FATAL("MACL not implemented"); }

// MAC.W   @Rm+,@Rn+
EMITTER(MACW) { LOG_FATAL("MACW not implemented"); }

// MUL.L   Rm,Rn
EMITTER(MULL) {
  Value *v =
      b.SMul(b.LoadRegister(i.Rn, VALUE_I32), b.LoadRegister(i.Rm, VALUE_I32));
  b.StoreContext(offsetof(SH4Context, macl), v);
}

// MULS    Rm,Rn
EMITTER(MULS) {
  Value *v = b.SMul(b.SExt(b.LoadRegister(i.Rn, VALUE_I16), VALUE_I32),
                    b.SExt(b.LoadRegister(i.Rm, VALUE_I16), VALUE_I32));
  b.StoreContext(offsetof(SH4Context, macl), v);
}

// MULU    Rm,Rn
EMITTER(MULU) {
  Value *v = b.UMul(b.ZExt(b.LoadRegister(i.Rn, VALUE_I16), VALUE_I32),
                    b.ZExt(b.LoadRegister(i.Rm, VALUE_I16), VALUE_I32));
  b.StoreContext(offsetof(SH4Context, macl), v);
}

// NEG     Rm,Rn
EMITTER(NEG) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreRegister(i.Rn, b.Neg(rm));
}

// NEGC    Rm,Rn
EMITTER(NEGC) {
  Value *t = b.LoadT();
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreRegister(i.Rn, b.Sub(b.Neg(rm), t));
  Value *carry = b.Or(t, rm);
  b.StoreT(carry);
}

// SUB     Rm,Rn
EMITTER(SUB) {
  Value *v =
      b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.LoadRegister(i.Rm, VALUE_I32));
  b.StoreRegister(i.Rn, v);
}

// SUBC    Rm,Rn
EMITTER(SUBC) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *v1 = b.Sub(rn, rm);
  Value *v2 = b.Sub(v1, b.LoadT());
  b.StoreRegister(i.Rn, v2);

  // if the available bits were overflowed, set the carry flag
  Value *rnrm_overflow = b.UGT(v1, rn);
  Value *rnrmt_overflow = b.UGT(v2, v1);
  Value *overflow = b.Or(rnrm_overflow, rnrmt_overflow);
  b.StoreT(overflow);
}

// SUBV    Rm,Rn
EMITTER(SUBV) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *v = b.Sub(rn, rm);
  b.StoreRegister(i.Rn, v);

  // if both Rm and Rn are the "same" sign (keeping in mind, subtracting a
  // negative is the same as adding a positive), but value is different,
  // overflowed
  Value *rm_ge_0 = b.SGE(rm, b.AllocConstant(0));
  Value *rn_ge_0 = b.SGE(rn, b.AllocConstant(0));
  Value *v_ge_0 = b.SGE(v, b.AllocConstant(0));
  Value *overflow = b.And(b.NE(rn_ge_0, rm_ge_0), b.EQ(rm_ge_0, v_ge_0));
  b.StoreT(overflow);
}

// code                 cycles  t-bit
// 0010 nnnn mmmm 1001  1       -
// AND     Rm,Rn
EMITTER(AND) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreRegister(i.Rn, b.And(rn, rm));
}

// code                 cycles  t-bit
// 1100 1001 iiii iiii  1       -
// AND     #imm,R0
EMITTER(ANDI) {
  Value *r0 = b.LoadRegister(0, VALUE_I32);
  Value *mask = b.AllocConstant((uint32_t)i.imm);
  b.StoreRegister(0, b.And(r0, mask));
}

// code                 cycles  t-bit
// 1100 1101 iiii iiii  1       -
// AND.B   #imm,@(R0,GBR)
EMITTER(ANDB) {
  Value *addr = b.Add(b.LoadRegister(0, VALUE_I32), b.LoadGBR());
  Value *v = b.Load(addr, VALUE_I8);
  b.Store(addr, b.And(v, b.AllocConstant((uint8_t)i.imm)));
}

// NOT     Rm,Rn
EMITTER(NOT) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreRegister(i.Rn, b.Not(rm));
}

// OR      Rm,Rn
EMITTER(OR) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreRegister(i.Rn, b.Or(rn, rm));
}

// OR      #imm,R0
EMITTER(ORI) {
  Value *r0 = b.LoadRegister(0, VALUE_I32);
  Value *imm = b.AllocConstant((uint32_t)i.imm);
  b.StoreRegister(0, b.Or(r0, imm));
}

// OR.B    #imm,@(R0,GBR)
EMITTER(ORB) {
  Value *addr = b.Add(b.LoadRegister(0, VALUE_I32), b.LoadGBR());
  Value *v = b.Load(addr, VALUE_I8);
  b.Store(addr, b.Or(v, b.AllocConstant((uint8_t)i.imm)));
}

// TAS.B   @Rn
EMITTER(TAS) {
  Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I8);
  b.Store(addr, b.Or(v, b.AllocConstant((uint8_t)0x80)));
  b.StoreT(b.EQ(v, b.AllocConstant((uint8_t)0)));
}

// TST     Rm,Rn
EMITTER(TST) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreT(b.EQ(b.And(rn, rm), b.AllocConstant(0)));
}

// TST     #imm,R0
EMITTER(TSTI) {
  Value *r0 = b.LoadRegister(0, VALUE_I32);
  Value *imm = b.AllocConstant((uint32_t)i.imm);
  b.StoreT(b.EQ(b.And(r0, imm), b.AllocConstant((uint32_t)0)));
}

// TST.B   #imm,@(R0,GBR)
EMITTER(TSTB) {
  Value *addr = b.Add(b.LoadRegister(0, VALUE_I32), b.LoadGBR());
  Value *v = b.Load(addr, VALUE_I8);
  Value *imm = b.AllocConstant((uint8_t)i.imm);
  b.StoreT(b.EQ(b.And(v, imm), b.AllocConstant((uint8_t)0)));
}

// XOR     Rm,Rn
EMITTER(XOR) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreRegister(i.Rn, b.Xor(rn, rm));
}

// XOR     #imm,R0
EMITTER(XORI) {
  Value *r0 = b.LoadRegister(0, VALUE_I32);
  Value *mask = b.AllocConstant((uint32_t)i.imm);
  b.StoreRegister(0, b.Xor(r0, mask));
}

// XOR.B   #imm,@(R0,GBR)
EMITTER(XORB) {
  Value *addr = b.Add(b.LoadRegister(0, VALUE_I32), b.LoadGBR());
  b.Store(addr, b.Xor(b.Load(addr, VALUE_I8), b.AllocConstant((uint8_t)i.imm)));
}

// ROTL    Rn
EMITTER(ROTL) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rn_msb = b.And(b.LShr(rn, 31), b.AllocConstant(0x1));
  b.StoreRegister(i.Rn, b.Or(b.Shl(rn, 1), rn_msb));
  b.StoreT(rn_msb);
}

// ROTR    Rn
EMITTER(ROTR) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rn_lsb = b.And(rn, b.AllocConstant(0x1));
  b.StoreRegister(i.Rn, b.Or(b.Shl(rn_lsb, 31), b.LShr(rn, 1)));
  b.StoreT(rn_lsb);
}

// ROTCL   Rn
EMITTER(ROTCL) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rn_msb = b.And(b.LShr(rn, 31), b.AllocConstant(0x1));
  b.StoreRegister(i.Rn, b.Or(b.Shl(rn, 1), b.LoadT()));
  b.StoreT(rn_msb);
}

// ROTCR   Rn
EMITTER(ROTCR) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rn_lsb = b.And(rn, b.AllocConstant(0x1));
  b.StoreRegister(i.Rn, b.Or(b.Shl(b.LoadT(), 31), b.LShr(rn, 1)));
  b.StoreT(rn_lsb);
}

// SHAD    Rm,Rn
EMITTER(SHAD) {
  // when Rm >= 0, Rn << Rm
  // when Rm < 0, Rn >> Rm
  // when shifting right > 32, Rn = (Rn >= 0 ? 0 : -1)
  Block *shl_block = b.AppendBlock();
  Block *shr_block = b.AppendBlock();
  Block *shr_nooverflow_block = b.AppendBlock();
  Block *shr_overflow_block = b.AppendBlock();
  Block *end_block = b.AppendBlock();

  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.BranchCond(b.SGE(rm, b.AllocConstant(0)), shl_block, shr_block);

  {
    b.SetCurrentBlock(shl_block);
    Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
    Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
    b.StoreRegister(i.Rn, b.Shl(rn, b.And(rm, b.AllocConstant(0x1f))));
    b.Branch(end_block);
  }

  {
    b.SetCurrentBlock(shr_block);

    Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
    b.BranchCond(b.And(rm, b.AllocConstant(0x1f)), shr_nooverflow_block,
                 shr_overflow_block);

    {
      b.SetCurrentBlock(shr_nooverflow_block);
      Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
      Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
      b.StoreRegister(i.Rn,
                      b.AShr(rn, b.Add(b.And(b.Not(rm), b.AllocConstant(0x1f)),
                                       b.AllocConstant(1))));
      b.Branch(end_block);
    }

    {
      b.SetCurrentBlock(shr_overflow_block);
      Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
      b.StoreRegister(i.Rn, b.Select(b.SGE(rn, b.AllocConstant(0)),
                                     b.AllocConstant(0), b.AllocConstant(-1)));
      b.Branch(end_block);
    }

    b.SetCurrentBlock(end_block);
  }
}

// SHAL    Rn      (same as SHLL)
EMITTER(SHAL) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rn_msb = b.And(b.LShr(rn, 31), b.AllocConstant(0x1));
  b.StoreRegister(i.Rn, b.Shl(rn, 1));
  b.StoreT(rn_msb);
}

// SHAR    Rn
EMITTER(SHAR) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rn_lsb = b.And(rn, b.AllocConstant(0x1));
  b.StoreRegister(i.Rn, b.AShr(rn, 1));
  b.StoreT(rn_lsb);
}

// SHLD    Rm,Rn
EMITTER(SHLD) {
  // when Rm >= 0, Rn << Rm
  // when Rm < 0, Rn >> Rm
  // when shifting right >= 32, Rn = 0
  Block *shl_block = b.AppendBlock();
  Block *shr_block = b.AppendBlock();
  Block *shr_nooverflow_block = b.AppendBlock();
  Block *shr_overflow_block = b.AppendBlock();
  Block *end_block = b.AppendBlock();

  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.BranchCond(b.SGE(rm, b.AllocConstant(0)), shl_block, shr_block);

  {
    b.SetCurrentBlock(shl_block);
    Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
    Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
    b.StoreRegister(i.Rn, b.Shl(rn, b.And(rm, b.AllocConstant(0x1f))));
    b.Branch(end_block);
  }

  {
    b.SetCurrentBlock(shr_block);
    Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
    b.BranchCond(b.And(rm, b.AllocConstant(0x1f)), shr_nooverflow_block,
                 shr_overflow_block);

    {
      b.SetCurrentBlock(shr_nooverflow_block);
      Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
      Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
      b.StoreRegister(i.Rn,
                      b.LShr(rn, b.Add(b.And(b.Not(rm), b.AllocConstant(0x1f)),
                                       b.AllocConstant(1))));
      b.Branch(end_block);
    }

    {
      b.SetCurrentBlock(shr_overflow_block);
      b.StoreRegister(i.Rn, b.AllocConstant(0));
      b.Branch(end_block);
    }
  }

  b.SetCurrentBlock(end_block);
}

// SHLL    Rn      (same as SHAL)
EMITTER(SHLL) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rn_msb = b.And(b.LShr(rn, 31), b.AllocConstant(0x1));
  b.StoreRegister(i.Rn, b.Shl(rn, 1));
  b.StoreT(rn_msb);
}

// SHLR    Rn
EMITTER(SHLR) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  Value *rn_lsb = b.And(rn, b.AllocConstant(0x1));
  b.StoreRegister(i.Rn, b.LShr(rn, 1));
  b.StoreT(rn_lsb);
}

// SHLL2   Rn
EMITTER(SHLL2) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreRegister(i.Rn, b.Shl(rn, 2));
}

// SHLR2   Rn
EMITTER(SHLR2) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreRegister(i.Rn, b.LShr(rn, 2));
}

// SHLL8   Rn
EMITTER(SHLL8) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreRegister(i.Rn, b.Shl(rn, 8));
}

// SHLR8   Rn
EMITTER(SHLR8) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreRegister(i.Rn, b.LShr(rn, 8));
}

// SHLL16  Rn
EMITTER(SHLL16) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreRegister(i.Rn, b.Shl(rn, 16));
}

// SHLR16  Rn
EMITTER(SHLR16) {
  Value *rn = b.LoadRegister(i.Rn, VALUE_I32);
  b.StoreRegister(i.Rn, b.LShr(rn, 16));
}

// code                 cycles  t-bit
// 1000 1011 dddd dddd  3/1     -
// BF      disp
EMITTER(BF) {
  uint32_t dest_addr = i.addr + 4 + (int8_t)i.disp * 2;
  Value *cond = b.LoadT();
  b.BranchFalse(cond, b.AllocConstant(dest_addr));
}

// code                 cycles  t-bit
// 1000 1111 dddd dddd  3/1     -
// BFS     disp
EMITTER(BFS) {
  b.PreserveT();
  b.EmitDelayInstr();
  Value *cond = b.LoadPreserved();

  uint32_t dest_addr = i.addr + 4 + (int8_t)i.disp * 2;
  b.BranchFalse(cond, b.AllocConstant(dest_addr));
}

// code                 cycles  t-bit
// 1000 1001 dddd dddd  3/1     -
// BT      disp
EMITTER(BT) {
  uint32_t dest_addr = i.addr + 4 + (int8_t)i.disp * 2;
  Value *cond = b.LoadT();
  b.BranchTrue(cond, b.AllocConstant(dest_addr));
}

// code                 cycles  t-bit
// 1000 1101 dddd dddd  2/1     -
// BTS     disp
EMITTER(BTS) {
  b.PreserveT();
  b.EmitDelayInstr();
  Value *cond = b.LoadPreserved();

  uint32_t dest_addr = i.addr + 4 + (int8_t)i.disp * 2;
  b.BranchTrue(cond, b.AllocConstant(dest_addr));
}

// code                 cycles  t-bit
// 1010 dddd dddd dddd  2       -
// BRA     disp
EMITTER(BRA) {
  b.EmitDelayInstr();

  int32_t disp = ((i.disp & 0xfff) << 20) >>
                 20;  // 12-bit displacement must be sign extended
  uint32_t dest_addr = i.addr + 4 + disp * 2;
  b.Branch(b.AllocConstant(dest_addr));
}

// code                 cycles  t-bit
// 0000 mmmm 0010 0011  2       -
// BRAF    Rn
EMITTER(BRAF) {
  b.PreserveRegister(i.Rn);
  b.EmitDelayInstr();
  Value *rn = b.LoadPreserved();

  Value *dest_addr = b.Add(b.AllocConstant(i.addr + 4), rn);
  b.Branch(dest_addr);
}

// code                 cycles  t-bit
// 1011 dddd dddd dddd  2       -
// BSR     disp
EMITTER(BSR) {
  b.EmitDelayInstr();

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
  b.PreserveRegister(i.Rn);
  b.EmitDelayInstr();
  Value *rn = b.LoadPreserved();

  Value *ret_addr = b.AllocConstant(i.addr + 4);
  Value *dest_addr = b.Add(rn, ret_addr);
  b.StorePR(ret_addr);
  b.Branch(dest_addr);
}

// JMP     @Rm
EMITTER(JMP) {
  b.PreserveRegister(i.Rn);
  b.EmitDelayInstr();
  Value *dest_addr = b.LoadPreserved();

  b.Branch(dest_addr);
}

// JSR     @Rn
EMITTER(JSR) {
  b.PreserveRegister(i.Rn);
  b.EmitDelayInstr();
  Value *dest_addr = b.LoadPreserved();

  Value *ret_addr = b.AllocConstant(i.addr + 4);
  b.StorePR(ret_addr);
  b.Branch(dest_addr);
}

// RTS
EMITTER(RTS) {
  b.PreservePR();
  b.EmitDelayInstr();
  Value *dest_addr = b.LoadPreserved();

  b.Branch(dest_addr);
}

// code                 cycles  t-bit
// 0000 0000 0010 1000  1       -
// CLRMAC
EMITTER(CLRMAC) {
  b.StoreContext(offsetof(SH4Context, mach), b.AllocConstant(0));
  b.StoreContext(offsetof(SH4Context, macl), b.AllocConstant(0));
}

EMITTER(CLRS) { b.StoreSR(b.And(b.LoadSR(), b.AllocConstant(~S))); }

// code                 cycles  t-bit
// 0000 0000 0000 1000  1       -
// CLRT
EMITTER(CLRT) { b.StoreT(b.AllocConstant(0)); }

// LDC     Rm,SR
EMITTER(LDCSR) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreSR(v);
}

// LDC     Rm,GBR
EMITTER(LDCGBR) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreGBR(v);
}

// LDC     Rm,VBR
EMITTER(LDCVBR) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, vbr), v);
}

// LDC     Rm,SSR
EMITTER(LDCSSR) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, ssr), v);
}

// LDC     Rm,SPC
EMITTER(LDCSPC) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, spc), v);
}

// LDC     Rm,DBR
EMITTER(LDCDBR) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, dbr), v);
}

// LDCRBANK   Rm,Rn_BANK
EMITTER(LDCRBANK) {
  Block *rb1 = b.AppendBlock();
  Block *rb0 = b.AppendBlock();
  Block *end_block = b.AppendBlock();

  int reg = i.Rn & 0x7;

  b.BranchCond(b.And(b.LoadSR(), b.AllocConstant(RB)), rb1, rb0);

  {
    b.SetCurrentBlock(rb1);
    Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
    b.StoreContext(offsetof(SH4Context, rbnk[0]) + reg * 4, rm);
    b.Branch(end_block);
  }

  {
    b.SetCurrentBlock(rb0);
    Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
    b.StoreContext(offsetof(SH4Context, rbnk[1]) + reg * 4, rm);
    b.Branch(end_block);
  }

  b.SetCurrentBlock(end_block);
}

// LDC.L   @Rm+,SR
EMITTER(LDCMSR) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreSR(v);
  // reload Rm, sr store could have swapped banks
  addr = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// LDC.L   @Rm+,GBR
EMITTER(LDCMGBR) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreGBR(v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// LDC.L   @Rm+,VBR
EMITTER(LDCMVBR) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, vbr), v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// LDC.L   @Rm+,SSR
EMITTER(LDCMSSR) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, ssr), v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// LDC.L   @Rm+,SPC
EMITTER(LDCMSPC) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, spc), v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// LDC.L   @Rm+,DBR
EMITTER(LDCMDBR) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, dbr), v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// LDC.L   @Rm+,Rn_BANK
EMITTER(LDCMRBANK) {
  Block *rb1 = b.AppendBlock();
  Block *rb0 = b.AppendBlock();
  Block *end_block = b.AppendBlock();

  int reg = i.Rn & 0x7;

  b.BranchCond(b.And(b.LoadSR(), b.AllocConstant(RB)), rb1, rb0);

  {
    b.SetCurrentBlock(rb0);
    Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
    b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
    Value *v = b.Load(addr, VALUE_I32);
    b.StoreContext(offsetof(SH4Context, rbnk[1]) + reg * 4, v);
    b.Branch(end_block);
  }

  {
    b.SetCurrentBlock(rb1);
    Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
    b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
    Value *v = b.Load(addr, VALUE_I32);
    b.StoreContext(offsetof(SH4Context, rbnk[0]) + reg * 4, v);
    b.Branch(end_block);
  }

  b.SetCurrentBlock(end_block);
}

// LDS     Rm,MACH
EMITTER(LDSMACH) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, mach), v);
}

// LDS     Rm,MACL
EMITTER(LDSMACL) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, macl), v);
}

// LDS     Rm,PR
EMITTER(LDSPR) {
  Value *v = b.LoadRegister(i.Rm, VALUE_I32);
  b.StorePR(v);
}

// LDS.L   @Rm+,MACH
EMITTER(LDSMMACH) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, mach), v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// LDS.L   @Rm+,MACL
EMITTER(LDSMMACL) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, macl), v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// LDS.L   @Rm+,PR
EMITTER(LDSMPR) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StorePR(v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// MOVCA.L     R0,@Rn
EMITTER(MOVCAL) { LOG_FATAL("MOVCAL not implemented"); }

// NOP
EMITTER(NOP) {}

// OCBI
EMITTER(OCBI) {}

// OCBP
EMITTER(OCBP) {}

// OCBWB
EMITTER(OCBWB) {}

// PREF     @Rn
// FIXME this is painfully bad
EMITTER(PREF) {
  Block *sq_block = b.AppendBlock();
  Block *sq1_block = b.AppendBlock();
  Block *sq0_block = b.AppendBlock();
  Block *end_block = b.AppendBlock();

  {
    Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
    Value *is_sq_call = b.And(b.UGE(addr, b.AllocConstant(0xe0000000)),
                              b.ULE(addr, b.AllocConstant(0xe3fffffc)));
    b.BranchCond(is_sq_call, sq_block, end_block);
  }

  {
    {
      b.SetCurrentBlock(sq_block);
      Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
      Value *sq = b.And(addr, b.AllocConstant(0x20));
      b.BranchCond(sq, sq1_block, sq0_block);
    }

    {
      b.SetCurrentBlock(sq1_block);
      Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
      Value *sq1_dest =
          b.Or(b.And(addr, b.AllocConstant(0x03ffffe0)),
               b.LoadContext(offsetof(SH4Context, sq_ext_addr[1]), VALUE_I32));
      for (int i = 0; i < 8; i++) {
        b.Store(sq1_dest,
                b.LoadContext(offsetof(SH4Context, sq[1]) + i * 4, VALUE_I32));
        sq1_dest = b.Add(sq1_dest, b.AllocConstant(4));
      }
      b.Branch(end_block);
    }

    {
      b.SetCurrentBlock(sq0_block);
      Value *addr = b.LoadRegister(i.Rn, VALUE_I32);
      Value *sq0_dest =
          b.Or(b.And(addr, b.AllocConstant(0x03ffffe0)),
               b.LoadContext(offsetof(SH4Context, sq_ext_addr[0]), VALUE_I32));
      for (int i = 0; i < 8; i++) {
        b.Store(sq0_dest,
                b.LoadContext(offsetof(SH4Context, sq[0]) + i * 4, VALUE_I32));
        sq0_dest = b.Add(sq0_dest, b.AllocConstant(4));
      }
      b.Branch(end_block);
    }
  }

  b.SetCurrentBlock(end_block);
}

// RTE
EMITTER(RTE) {
  Value *ssr = b.LoadContext(offsetof(SH4Context, ssr), VALUE_I32);
  Value *spc = b.LoadContext(offsetof(SH4Context, spc), VALUE_I32);
  b.StoreSR(ssr);
  b.Branch(spc);
}

EMITTER(SETS) { b.StoreSR(b.Or(b.LoadSR(), b.AllocConstant(S))); }

// SETT
EMITTER(SETT) { b.StoreT(b.AllocConstant(1)); }

// SLEEP
EMITTER(SLEEP) { LOG_FATAL("SLEEP not implemented"); }

// STC     SR,Rn
EMITTER(STCSR) {
  Value *v = b.LoadSR();
  b.StoreRegister(i.Rn, v);
}

// STC     GBR,Rn
EMITTER(STCGBR) {
  Value *v = b.LoadGBR();
  b.StoreRegister(i.Rn, v);
}

// STC     VBR,Rn
EMITTER(STCVBR) {
  Value *v = b.LoadContext(offsetof(SH4Context, vbr), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// STC     SSR,Rn
EMITTER(STCSSR) {
  Value *v = b.LoadContext(offsetof(SH4Context, ssr), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// STC     SPC,Rn
EMITTER(STCSPC) {
  Value *v = b.LoadContext(offsetof(SH4Context, spc), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// STC     SGR,Rn
EMITTER(STCSGR) {
  Value *v = b.LoadContext(offsetof(SH4Context, sgr), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// STC     DBR,Rn
EMITTER(STCDBR) {
  Value *v = b.LoadContext(offsetof(SH4Context, dbr), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// STC     Rm_BANK,Rn
EMITTER(STCRBANK) {
  Block *rb1 = b.AppendBlock();
  Block *rb0 = b.AppendBlock();
  Block *end_block = b.AppendBlock();

  int reg = i.Rm & 0x7;

  b.BranchCond(b.And(b.LoadSR(), b.AllocConstant(RB)), rb1, rb0);

  {
    b.SetCurrentBlock(rb1);
    b.StoreRegister(i.Rn, b.LoadContext(offsetof(SH4Context, rbnk[0]) + reg * 4,
                                        VALUE_I32));
    b.Branch(end_block);
  }

  {
    b.SetCurrentBlock(rb0);
    b.StoreRegister(i.Rn, b.LoadContext(offsetof(SH4Context, rbnk[1]) + reg * 4,
                                        VALUE_I32));
    b.Branch(end_block);
  }

  b.SetCurrentBlock(end_block);
}

// STC.L   SR,@-Rn
EMITTER(STCMSR) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);
  Value *v = b.LoadSR();
  b.Store(addr, v);
}

// STC.L   GBR,@-Rn
EMITTER(STCMGBR) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);
  Value *v = b.LoadGBR();
  b.Store(addr, v);
}

// STC.L   VBR,@-Rn
EMITTER(STCMVBR) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, vbr), VALUE_I32);
  b.Store(addr, v);
}

// STC.L   SSR,@-Rn
EMITTER(STCMSSR) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, ssr), VALUE_I32);
  b.Store(addr, v);
}

// STC.L   SPC,@-Rn
EMITTER(STCMSPC) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, spc), VALUE_I32);
  b.Store(addr, v);
}

// STC.L   SGR,@-Rn
EMITTER(STCMSGR) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, sgr), VALUE_I32);
  b.Store(addr, v);
}

// STC.L   DBR,@-Rn
EMITTER(STCMDBR) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);
  Value *v = b.LoadContext(offsetof(SH4Context, dbr), VALUE_I32);
  b.Store(addr, v);
}

// STC.L   Rm_BANK,@-Rn
EMITTER(STCMRBANK) {
  Block *rb1 = b.AppendBlock();
  Block *rb0 = b.AppendBlock();
  Block *end_block = b.AppendBlock();

  int reg = i.Rm & 0x7;

  b.BranchCond(b.And(b.LoadSR(), b.AllocConstant(RB)), rb1, rb0);

  {
    b.SetCurrentBlock(rb1);
    Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
    b.StoreRegister(i.Rn, addr);
    b.Store(addr,
            b.LoadContext(offsetof(SH4Context, rbnk[0]) + reg * 4, VALUE_I32));
    b.Branch(end_block);
  }

  {
    b.SetCurrentBlock(rb0);
    Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
    b.StoreRegister(i.Rn, addr);
    b.Store(addr,
            b.LoadContext(offsetof(SH4Context, rbnk[1]) + reg * 4, VALUE_I32));
    b.Branch(end_block);
  }

  b.SetCurrentBlock(end_block);
}

// STS     MACH,Rn
EMITTER(STSMACH) {
  Value *v = b.LoadContext(offsetof(SH4Context, mach), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// STS     MACL,Rn
EMITTER(STSMACL) {
  Value *v = b.LoadContext(offsetof(SH4Context, macl), VALUE_I32);
  b.StoreRegister(i.Rn, v);
}

// STS     PR,Rn
EMITTER(STSPR) {
  Value *v = b.LoadPR();
  b.StoreRegister(i.Rn, v);
}

// STS.L   MACH,@-Rn
EMITTER(STSMMACH) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);

  Value *mach = b.LoadContext(offsetof(SH4Context, mach), VALUE_I32);
  b.Store(addr, mach);
}

// STS.L   MACL,@-Rn
EMITTER(STSMMACL) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);

  Value *macl = b.LoadContext(offsetof(SH4Context, macl), VALUE_I32);
  b.Store(addr, macl);
}

// STS.L   PR,@-Rn
EMITTER(STSMPR) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);

  Value *pr = b.LoadPR();
  b.Store(addr, pr);
}

// TRAPA   #imm
EMITTER(TRAPA) { LOG_FATAL("TRAPA not implemented"); }

// FLDI0  FRn 1111nnnn10001101
EMITTER(FLDI0) { b.StoreRegisterF(i.Rn, b.AllocConstant(0)); }

// FLDI1  FRn 1111nnnn10011101
EMITTER(FLDI1) { b.StoreRegisterF(i.Rn, b.AllocConstant(0x3F800000)); }

// FMOV    FRm,FRn PR=0 SZ=0 FRm -> FRn 1111nnnnmmmm1100
// FMOV    DRm,DRn PR=0 SZ=1 DRm -> DRn 1111nnn0mmm01100
// FMOV    XDm,DRn PR=1      XDm -> DRn 1111nnn0mmm11100
// FMOV    DRm,XDn PR=1      DRm -> XDn 1111nnn1mmm01100
// FMOV    XDm,XDn PR=1      XDm -> XDn 1111nnn1mmm11100
EMITTER(FMOV) {
  if (fpu.double_precision || fpu.single_precision_pair) {
    if (i.Rm & 1) {
      if (i.Rn & 1) {
        b.StoreRegisterXF(i.Rn & 0xe, b.LoadRegisterXF(i.Rm & 0xe, VALUE_I32));
        b.StoreRegisterXF(i.Rn | 0x1, b.LoadRegisterXF(i.Rm | 0x1, VALUE_I32));
      } else {
        b.StoreRegisterF(i.Rn & 0xe, b.LoadRegisterXF(i.Rm & 0xe, VALUE_I32));
        b.StoreRegisterF(i.Rn | 0x1, b.LoadRegisterXF(i.Rm | 0x1, VALUE_I32));
      }
    } else {
      if (i.Rn & 1) {
        b.StoreRegisterXF(i.Rn & 0xe, b.LoadRegisterF(i.Rm & 0xe, VALUE_I32));
        b.StoreRegisterXF(i.Rn | 0x1, b.LoadRegisterF(i.Rm | 0x1, VALUE_I32));
      } else {
        b.StoreRegisterF(i.Rn & 0xe, b.LoadRegisterF(i.Rm & 0xe, VALUE_I32));
        b.StoreRegisterF(i.Rn | 0x1, b.LoadRegisterF(i.Rm | 0x1, VALUE_I32));
      }
    }
  } else {
    b.StoreRegisterF(i.Rn, b.LoadRegisterF(i.Rm, VALUE_I32));
  }
}

// FMOV.S  @Rm,FRn PR=0 SZ=0 1111nnnnmmmm1000
// FMOV    @Rm,DRn PR=0 SZ=1 1111nnn0mmmm1000
// FMOV    @Rm,XDn PR=0 SZ=1 1111nnn1mmmm1000
// FMOV    @Rm,XDn PR=1      1111nnn1mmmm1000
// FMOV    @Rm,DRn PR=1      1111nnn0mmmm1000
EMITTER(FMOVLD) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);

  if (fpu.double_precision) {
    if (i.Rn & 1) {
      b.StoreRegisterXF(i.Rn | 0x1, b.Load(addr, VALUE_I32));
      b.StoreRegisterXF(i.Rn & 0xe,
                        b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
    } else {
      b.StoreRegisterF(i.Rn | 0x1, b.Load(addr, VALUE_I32));
      b.StoreRegisterF(i.Rn & 0xe,
                       b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
    }
  } else if (fpu.single_precision_pair) {
    if (i.Rn & 1) {
      b.StoreRegisterXF(i.Rn & 0xe, b.Load(addr, VALUE_I32));
      b.StoreRegisterXF(i.Rn | 0x1,
                        b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
    } else {
      b.StoreRegisterF(i.Rn & 0xe, b.Load(addr, VALUE_I32));
      b.StoreRegisterF(i.Rn | 0x1,
                       b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
    }
  } else {
    b.StoreRegisterF(i.Rn, b.Load(addr, VALUE_I32));
  }
}

// FMOV.S  @(R0,Rm),FRn PR=0 SZ=0 1111nnnnmmmm0110
// FMOV    @(R0,Rm),DRn PR=0 SZ=1 1111nnn0mmmm0110
// FMOV    @(R0,Rm),XDn PR=0 SZ=1 1111nnn1mmmm0110
// FMOV    @(R0,Rm),XDn PR=1      1111nnn1mmmm0110
EMITTER(FMOVILD) {
  Value *addr =
      b.Add(b.LoadRegister(0, VALUE_I32), b.LoadRegister(i.Rm, VALUE_I32));

  // FMOV with PR=1 assumes the values are word-swapped in memory
  if (fpu.double_precision) {
    b.StoreRegisterXF(i.Rn | 0x1, b.Load(addr, VALUE_I32));
    b.StoreRegisterXF(i.Rn & 0xe,
                      b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
  } else if (fpu.single_precision_pair) {
    if (i.Rn & 1) {
      b.StoreRegisterXF(i.Rn & 0xe, b.Load(addr, VALUE_I32));
      b.StoreRegisterXF(i.Rn | 0x1,
                        b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
    } else {
      b.StoreRegisterF(i.Rn & 0xe, b.Load(addr, VALUE_I32));
      b.StoreRegisterF(i.Rn | 0x1,
                       b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
    }
  } else {
    b.StoreRegisterF(i.Rn, b.Load(addr, VALUE_I32));
  }
}

// FMOV.S  @Rm+,FRn PR=0 SZ=0 1111nnnnmmmm1001
// FMOV    @Rm+,DRn PR=0 SZ=1 1111nnn0mmmm1001
// FMOV    @Rm+,XDn PR=0 SZ=1 1111nnn1mmmm1001
// FMOV    @Rm+,XDn PR=1      1111nnn1mmmm1001
EMITTER(FMOVRS) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);

  // FMOV with PR=1 assumes the values are word-swapped in memory
  if (fpu.double_precision) {
    b.StoreRegisterXF(i.Rn | 0x1, b.Load(addr, VALUE_I32));
    b.StoreRegisterXF(i.Rn & 0xe,
                      b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
    b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(8)));
  } else if (fpu.single_precision_pair) {
    if (i.Rn & 1) {
      b.StoreRegisterXF(i.Rn & 0xe, b.Load(addr, VALUE_I32));
      b.StoreRegisterXF(i.Rn | 0x1,
                        b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
    } else {
      b.StoreRegisterF(i.Rn & 0xe, b.Load(addr, VALUE_I32));
      b.StoreRegisterF(i.Rn | 0x1,
                       b.Load(b.Add(addr, b.AllocConstant(4)), VALUE_I32));
    }
    b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(8)));
  } else {
    b.StoreRegisterF(i.Rn, b.Load(addr, VALUE_I32));
    b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
  }
}

// FMOV.S  FRm,@Rn PR=0 SZ=0 1111nnnnmmmm1010
// FMOV    DRm,@Rn PR=0 SZ=1 1111nnnnmmm01010
// FMOV    XDm,@Rn PR=0 SZ=1 1111nnnnmmm11010
// FMOV    XDm,@Rn PR=1      1111nnnnmmm11010
EMITTER(FMOVST) {
  Value *addr = b.LoadRegister(i.Rn, VALUE_I32);

  if (fpu.double_precision) {
    b.Store(addr, b.LoadRegisterXF(i.Rm | 0x1, VALUE_I32));
    b.Store(b.Add(addr, b.AllocConstant(4)),
            b.LoadRegisterXF(i.Rm & 0xe, VALUE_I32));
  } else if (fpu.single_precision_pair) {
    if (i.Rm & 1) {
      b.Store(addr, b.LoadRegisterXF(i.Rm & 0xe, VALUE_I32));
      b.Store(b.Add(addr, b.AllocConstant(4)),
              b.LoadRegisterXF(i.Rm | 0x1, VALUE_I32));
    } else {
      b.Store(addr, b.LoadRegisterF(i.Rm & 0xe, VALUE_I32));
      b.Store(b.Add(addr, b.AllocConstant(4)),
              b.LoadRegisterF(i.Rm | 0x1, VALUE_I32));
    }
  } else {
    b.Store(addr, b.LoadRegisterF(i.Rm, VALUE_I32));
  }
}

// FMOV.S  FRm,@-Rn PR=0 SZ=0 1111nnnnmmmm1011
// FMOV    DRm,@-Rn PR=0 SZ=1 1111nnnnmmm01011
// FMOV    XDm,@-Rn PR=0 SZ=1 1111nnnnmmm11011
// FMOV    XDm,@-Rn PR=1      1111nnnnmmm11011
EMITTER(FMOVSV) {
  if (fpu.double_precision) {
    Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(8));
    b.StoreRegister(i.Rn, addr);
    b.Store(addr, b.LoadRegisterXF(i.Rm | 0x1, VALUE_I32));
    b.Store(b.Add(addr, b.AllocConstant(4)),
            b.LoadRegisterXF(i.Rm & 0xe, VALUE_I32));
  } else if (fpu.single_precision_pair) {
    Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(8));
    b.StoreRegister(i.Rn, addr);
    if (i.Rm & 1) {
      b.Store(addr, b.LoadRegisterXF(i.Rm & 0xe, VALUE_I32));
      b.Store(b.Add(addr, b.AllocConstant(4)),
              b.LoadRegisterXF(i.Rm | 0x1, VALUE_I32));
    } else {
      b.Store(addr, b.LoadRegisterF(i.Rm & 0xe, VALUE_I32));
      b.Store(b.Add(addr, b.AllocConstant(4)),
              b.LoadRegisterF(i.Rm | 0x1, VALUE_I32));
    }
  } else {
    Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
    b.StoreRegister(i.Rn, addr);
    b.Store(addr, b.LoadRegisterF(i.Rm, VALUE_I32));
  }
}

// FMOV.S  FRm,@(R0,Rn) PR=0 SZ=0 1111nnnnmmmm0111
// FMOV    DRm,@(R0,Rn) PR=0 SZ=1 1111nnnnmmm00111
// FMOV    XDm,@(R0,Rn) PR=0 SZ=1 1111nnnnmmm10111
// FMOV    XDm,@(R0,Rn) PR=1      1111nnnnmmm10111
EMITTER(FMOVIST) {
  Value *addr =
      b.Add(b.LoadRegister(0, VALUE_I32), b.LoadRegister(i.Rn, VALUE_I32));

  if (fpu.double_precision) {
    b.Store(addr, b.LoadRegisterXF(i.Rm | 0x1, VALUE_I32));
    b.Store(b.Add(addr, b.AllocConstant(4)),
            b.LoadRegisterXF(i.Rm & 0xe, VALUE_I32));
  } else if (fpu.single_precision_pair) {
    if (i.Rm & 1) {
      b.Store(addr, b.LoadRegisterXF(i.Rm & 0xe, VALUE_I32));
      b.Store(b.Add(addr, b.AllocConstant(4)),
              b.LoadRegisterXF(i.Rm | 0x1, VALUE_I32));
    } else {
      b.Store(addr, b.LoadRegisterF(i.Rm & 0xe, VALUE_I32));
      b.Store(b.Add(addr, b.AllocConstant(4)),
              b.LoadRegisterF(i.Rm | 0x1, VALUE_I32));
    }
  } else {
    b.Store(addr, b.LoadRegisterF(i.Rm, VALUE_I32));
  }
}

// FLDS FRm,FPUL 1111mmmm00011101
EMITTER(FLDS) {
  Value *rn = b.LoadRegisterF(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, fpul), rn);
}

// FSTS FPUL,FRn 1111nnnn00001101
EMITTER(FSTS) {
  Value *fpul = b.LoadContext(offsetof(SH4Context, fpul), VALUE_I32);
  b.StoreRegisterF(i.Rn, fpul);
}

// FABS FRn PR=0 1111nnnn01011101
// FABS DRn PR=1 1111nnn001011101
EMITTER(FABS) {
  if (fpu.double_precision) {
    int n = i.Rn & 0xe;
    b.StoreRegisterF(n, b.Abs(b.LoadRegisterF(n, VALUE_F64)));
  } else {
    b.StoreRegisterF(i.Rn, b.Abs(b.LoadRegisterF(i.Rn, VALUE_F32)));
  }
}

// FSRRA FRn PR=0 1111nnnn01111101
EMITTER(FSRRA) {
  Value *frn = b.LoadRegisterF(i.Rn, VALUE_F32);
  b.StoreRegisterF(i.Rn, b.Div(b.AllocConstant(1.0f), b.Sqrt(frn)));
}

// FADD FRm,FRn PR=0 1111nnnnmmmm0000
// FADD DRm,DRn PR=1 1111nnn0mmm00000
EMITTER(FADD) {
  if (fpu.double_precision) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    b.StoreRegisterF(
        n, b.Add(b.LoadRegisterF(n, VALUE_F64), b.LoadRegisterF(m, VALUE_F64)));
  } else {
    b.StoreRegisterF(i.Rn, b.Add(b.LoadRegisterF(i.Rn, VALUE_F32),
                                 b.LoadRegisterF(i.Rm, VALUE_F32)));
  }
}

// FCMP/EQ FRm,FRn PR=0 1111nnnnmmmm0100
// FCMP/EQ DRm,DRn PR=1 1111nnn0mmm00100
EMITTER(FCMPEQ) {
  if (fpu.double_precision) {
    b.StoreT(b.EQ(b.LoadRegisterF(i.Rn & 0xe, VALUE_F64),
                  b.LoadRegisterF(i.Rm & 0xe, VALUE_F64)));
  } else {
    b.StoreT(b.EQ(b.LoadRegisterF(i.Rn, VALUE_F32),
                  b.LoadRegisterF(i.Rm, VALUE_F32)));
  }
}

// FCMP/GT FRm,FRn PR=0 1111nnnnmmmm0101
// FCMP/GT DRm,DRn PR=1 1111nnn0mmm00101
EMITTER(FCMPGT) {
  if (fpu.double_precision) {
    b.StoreT(b.SGT(b.LoadRegisterF(i.Rn & 0xe, VALUE_F64),
                   b.LoadRegisterF(i.Rm & 0xe, VALUE_F64)));
  } else {
    b.StoreT(b.SGT(b.LoadRegisterF(i.Rn, VALUE_F32),
                   b.LoadRegisterF(i.Rm, VALUE_F32)));
  }
}

// FDIV FRm,FRn PR=0 1111nnnnmmmm0011
// FDIV DRm,DRn PR=1 1111nnn0mmm00011
EMITTER(FDIV) {
  if (fpu.double_precision) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    b.StoreRegisterF(
        n, b.Div(b.LoadRegisterF(n, VALUE_F64), b.LoadRegisterF(m, VALUE_F64)));

  } else {
    b.StoreRegisterF(i.Rn, b.Div(b.LoadRegisterF(i.Rn, VALUE_F32),
                                 b.LoadRegisterF(i.Rm, VALUE_F32)));
  }
}

// FLOAT FPUL,FRn PR=0 1111nnnn00101101
// FLOAT FPUL,DRn PR=1 1111nnn000101101
EMITTER(FLOAT) {
  Value *fpul = b.LoadContext(offsetof(SH4Context, fpul), VALUE_I32);

  if (fpu.double_precision) {
    b.StoreRegisterF(i.Rn & 0xe, b.Cast(b.SExt(fpul, VALUE_I64), VALUE_F64));
  } else {
    b.StoreRegisterF(i.Rn, b.Cast(fpul, VALUE_F32));
  }
}

// FMAC FR0,FRm,FRn PR=0 1111nnnnmmmm1110
EMITTER(FMAC) {
  if (!fpu.double_precision) {
    Value *rm = b.LoadRegisterF(i.Rm, VALUE_F32);
    b.StoreRegisterF(i.Rn, b.Add(b.SMul(b.LoadRegisterF(0, VALUE_F32), rm),
                                 b.LoadRegisterF(i.Rn, VALUE_F32)));
  }
}

// FMUL FRm,FRn PR=0 1111nnnnmmmm0010
// FMUL DRm,DRn PR=1 1111nnn0mmm00010
EMITTER(FMUL) {
  if (fpu.double_precision) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    b.StoreRegisterF(n, b.SMul(b.LoadRegisterF(n, VALUE_F64),
                               b.LoadRegisterF(m, VALUE_F64)));
  } else {
    b.StoreRegisterF(i.Rn, b.SMul(b.LoadRegisterF(i.Rn, VALUE_F32),
                                  b.LoadRegisterF(i.Rm, VALUE_F32)));
  }
}

// FNEG FRn PR=0 1111nnnn01001101
// FNEG DRn PR=1 1111nnn001001101
EMITTER(FNEG) {
  if (fpu.double_precision) {
    int n = i.Rn & 0xe;
    b.StoreRegisterF(n, b.Neg(b.LoadRegisterF(n, VALUE_F64)));
  } else {
    b.StoreRegisterF(i.Rn, b.Neg(b.LoadRegisterF(i.Rn, VALUE_F32)));
  }
}

// FSQRT FRn PR=0 1111nnnn01101101
// FSQRT DRn PR=1 1111nnnn01101101
EMITTER(FSQRT) {
  if (fpu.double_precision) {
    int n = i.Rn & 0xe;
    b.StoreRegisterF(n, b.Sqrt(b.LoadRegisterF(n, VALUE_F64)));
  } else {
    b.StoreRegisterF(i.Rn, b.Sqrt(b.LoadRegisterF(i.Rn, VALUE_F32)));
  }
}

// FSUB FRm,FRn PR=0 1111nnnnmmmm0001
// FSUB DRm,DRn PR=1 1111nnn0mmm00001
EMITTER(FSUB) {
  if (fpu.double_precision) {
    int n = i.Rn & 0xe;
    int m = i.Rm & 0xe;
    b.StoreRegisterF(
        n, b.Sub(b.LoadRegisterF(n, VALUE_F64), b.LoadRegisterF(m, VALUE_F64)));
  } else {
    b.StoreRegisterF(i.Rn, b.Sub(b.LoadRegisterF(i.Rn, VALUE_F32),
                                 b.LoadRegisterF(i.Rm, VALUE_F32)));
  }
}

// FTRC FRm,FPUL PR=0 1111mmmm00111101
// FTRC DRm,FPUL PR=1 1111mmm000111101
EMITTER(FTRC) {
  if (fpu.double_precision) {
    // FIXME is this truncate correct?
    Value *dpv = b.Truncate(b.Cast(b.LoadRegisterF(i.Rm, VALUE_F64), VALUE_I64),
                            VALUE_I32);
    b.StoreContext(offsetof(SH4Context, fpul), dpv);
  } else {
    Value *spv = b.Cast(b.LoadRegisterF(i.Rm, VALUE_F32), VALUE_I32);
    b.StoreContext(offsetof(SH4Context, fpul), spv);
  }
}

// FCNVDS DRm,FPUL PR=1 1111mmm010111101
EMITTER(FCNVDS) { LOG_FATAL("FCNVDS not implemented"); }

// FCNVSD FPUL, DRn PR=1 1111nnn010101101
EMITTER(FCNVSD) { LOG_FATAL("FCNVSD not implemented"); }

// LDS     Rm,FPSCR
EMITTER(LDSFPSCR) { b.StoreFPSCR(b.LoadRegister(i.Rm, VALUE_I32)); }

// LDS     Rm,FPUL
EMITTER(LDSFPUL) {
  Value *rm = b.LoadRegister(i.Rm, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, fpul), rm);
}

// LDS.L   @Rm+,FPSCR
EMITTER(LDSMFPSCR) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreFPSCR(v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// LDS.L   @Rm+,FPUL
EMITTER(LDSMFPUL) {
  Value *addr = b.LoadRegister(i.Rm, VALUE_I32);
  Value *v = b.Load(addr, VALUE_I32);
  b.StoreContext(offsetof(SH4Context, fpul), v);
  b.StoreRegister(i.Rm, b.Add(addr, b.AllocConstant(4)));
}

// STS     FPSCR,Rn
EMITTER(STSFPSCR) { b.StoreRegister(i.Rn, b.LoadFPSCR()); }

// STS     FPUL,Rn
EMITTER(STSFPUL) {
  Value *fpul = b.LoadContext(offsetof(SH4Context, fpul), VALUE_I32);
  b.StoreRegister(i.Rn, fpul);
}

// STS.L   FPSCR,@-Rn
EMITTER(STSMFPSCR) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);
  b.Store(addr, b.LoadFPSCR());
}

// STS.L   FPUL,@-Rn
EMITTER(STSMFPUL) {
  Value *addr = b.Sub(b.LoadRegister(i.Rn, VALUE_I32), b.AllocConstant(4));
  b.StoreRegister(i.Rn, addr);
  Value *fpul = b.LoadContext(offsetof(SH4Context, fpul), VALUE_I32);
  b.Store(addr, fpul);
}

// FIPR FVm,FVn PR=0 1111nnmm11101101
EMITTER(FIPR) {
  int m = i.Rm << 2;
  int n = i.Rn << 2;

  Value *p[4];
  for (int i = 0; i < 4; i++) {
    p[i] = b.SMul(b.LoadRegisterF(m + i, VALUE_F32),
                  b.LoadRegisterF(n + i, VALUE_F32));
  }

  b.StoreRegisterF(n + 3, b.Add(b.Add(b.Add(p[0], p[1]), p[2]), p[3]));
}

// FSCA FPUL,DRn PR=0 1111nnn011111101
EMITTER(FSCA) {
  int n = i.Rn << 1;

  Value *angle = b.SMul(
      b.SMul(b.Div(b.Cast(b.ZExt(b.LoadContext(offsetof(SH4Context, fpul),
                                               VALUE_I16),
                                 VALUE_I32),
                          VALUE_F32),
                   b.AllocConstant(65536.0f)),
             b.AllocConstant(2.0f)),
      b.AllocConstant((float)M_PI));
  b.StoreRegisterF(n, b.Sin(angle));
  b.StoreRegisterF(n + 1, b.Cos(angle));
}

// FTRV XMTRX,FVn PR=0 1111nn0111111101
EMITTER(FTRV) {
  Value *sum[4];
  int n = i.Rn << 2;

  sum[0] = b.Add(b.Add(b.Add(b.SMul(b.LoadRegisterXF(0, VALUE_F32),
                                    b.LoadRegisterF(n + 0, VALUE_F32)),
                             b.SMul(b.LoadRegisterXF(4, VALUE_F32),
                                    b.LoadRegisterF(n + 1, VALUE_F32))),
                       b.SMul(b.LoadRegisterXF(8, VALUE_F32),
                              b.LoadRegisterF(n + 2, VALUE_F32))),
                 b.SMul(b.LoadRegisterXF(12, VALUE_F32),
                        b.LoadRegisterF(n + 3, VALUE_F32)));

  sum[1] = b.Add(b.Add(b.Add(b.SMul(b.LoadRegisterXF(1, VALUE_F32),
                                    b.LoadRegisterF(n + 0, VALUE_F32)),
                             b.SMul(b.LoadRegisterXF(5, VALUE_F32),
                                    b.LoadRegisterF(n + 1, VALUE_F32))),
                       b.SMul(b.LoadRegisterXF(9, VALUE_F32),
                              b.LoadRegisterF(n + 2, VALUE_F32))),
                 b.SMul(b.LoadRegisterXF(13, VALUE_F32),
                        b.LoadRegisterF(n + 3, VALUE_F32)));

  sum[2] = b.Add(b.Add(b.Add(b.SMul(b.LoadRegisterXF(2, VALUE_F32),
                                    b.LoadRegisterF(n + 0, VALUE_F32)),
                             b.SMul(b.LoadRegisterXF(6, VALUE_F32),
                                    b.LoadRegisterF(n + 1, VALUE_F32))),
                       b.SMul(b.LoadRegisterXF(10, VALUE_F32),
                              b.LoadRegisterF(n + 2, VALUE_F32))),
                 b.SMul(b.LoadRegisterXF(14, VALUE_F32),
                        b.LoadRegisterF(n + 3, VALUE_F32)));

  sum[3] = b.Add(b.Add(b.Add(b.SMul(b.LoadRegisterXF(3, VALUE_F32),
                                    b.LoadRegisterF(n + 0, VALUE_F32)),
                             b.SMul(b.LoadRegisterXF(7, VALUE_F32),
                                    b.LoadRegisterF(n + 1, VALUE_F32))),
                       b.SMul(b.LoadRegisterXF(11, VALUE_F32),
                              b.LoadRegisterF(n + 2, VALUE_F32))),
                 b.SMul(b.LoadRegisterXF(15, VALUE_F32),
                        b.LoadRegisterF(n + 3, VALUE_F32)));

  b.StoreRegisterF(n + 0, sum[0]);
  b.StoreRegisterF(n + 1, sum[1]);
  b.StoreRegisterF(n + 2, sum[2]);
  b.StoreRegisterF(n + 3, sum[3]);
}

// FRCHG 1111101111111101
EMITTER(FRCHG) { b.StoreFPSCR(b.Xor(b.LoadFPSCR(), b.AllocConstant(FR))); }

// FSCHG 1111001111111101
EMITTER(FSCHG) {  //
  b.StoreFPSCR(b.Xor(b.LoadFPSCR(), b.AllocConstant(SZ)));
}
