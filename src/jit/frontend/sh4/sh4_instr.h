INSTR(INVALID) {
  INVALID_INSTR();
}

/* MOV     #imm,Rn */
INSTR(MOVI) {
  int32_t v = (int32_t)(int8_t)(i.imm.imm);
  STORE_GPR_IMM_I32(i.imm.rn, v);
  NEXT_INSTR();
}

/* MOV.W   @(disp,PC),Rn */
INSTR(MOVWL_PCR) {
  uint32_t ea = (i.imm.imm * 2) + addr + 4;
  I32 v = SEXT_I16_I32(LOAD_IMM_I16(ea));
  STORE_GPR_I32(i.imm.rn, v);
  NEXT_INSTR();
}

/* MOV.L   @(disp,PC),Rn */
INSTR(MOVLL_PCR) {
  uint32_t ea = (i.imm.imm * 4) + (addr & ~3) + 4;
  I32 v = LOAD_IMM_I32(ea);
  STORE_GPR_I32(i.imm.rn, v);
  NEXT_INSTR();
}

/* MOV     Rm,Rn */
INSTR(MOV) {
  I32 v = LOAD_GPR_I32(i.def.rm);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* MOV.B   Rm,@Rn */
INSTR(MOVBS_IND) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I8 v = LOAD_GPR_I8(i.def.rm);
  STORE_I8(ea, v);
  NEXT_INSTR();
}

/* MOV.W   Rm,@Rn */
INSTR(MOVWS_IND) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I16 v = LOAD_GPR_I16(i.def.rm);
  STORE_I16(ea, v);
  NEXT_INSTR();
}

/* MOV.L   Rm,@Rn */
INSTR(MOVLS_IND) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_GPR_I32(i.def.rm);
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* MOV.B   @Rm,Rn */
INSTR(MOVBL_IND) {
  I32 ea = LOAD_GPR_I32(i.def.rm);
  I32 v = LOAD_I8(ea);
  v = SEXT_I8_I32(v);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* MOV.W   @Rm,Rn */
INSTR(MOVWL_IND) {
  I32 ea = LOAD_GPR_I32(i.def.rm);
  I32 v = LOAD_I16(ea);
  v = SEXT_I16_I32(v);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* MOV.L   @Rm,Rn */
INSTR(MOVLL_IND) {
  I32 ea = LOAD_GPR_I32(i.def.rm);
  I32 v = LOAD_I32(ea);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* MOV.B   Rm,@-Rn */
INSTR(MOVBS_DEC) {
  /* load Rm before decrementing Rn in case Rm == Rn */
  I8 v = LOAD_GPR_I8(i.def.rm);

  /* decrease Rn by 1 */
  I32 ea = LOAD_GPR_I32(i.def.rn);
  ea = SUB_IMM_I32(ea, 1);
  STORE_GPR_I32(i.def.rn, ea);

  /* store Rm at (Rn) */
  STORE_I8(ea, v);
  NEXT_INSTR();
}

/* MOV.W   Rm,@-Rn */
INSTR(MOVWS_DEC) {
  /* load Rm before decrementing Rn in case Rm == Rn */
  I16 v = LOAD_GPR_I16(i.def.rm);

  /* decrease Rn by 2 */
  I32 ea = LOAD_GPR_I32(i.def.rn);
  ea = SUB_IMM_I32(ea, 2);
  STORE_GPR_I32(i.def.rn, ea);

  /* store Rm at (Rn) */
  STORE_I16(ea, v);
  NEXT_INSTR();
}

/* MOV.L   Rm,@-Rn */
INSTR(MOVLS_DEC) {
  /* load Rm before decrementing Rn in case Rm == Rn */
  I32 v = LOAD_GPR_I32(i.def.rm);

  /* decrease Rn by 4 */
  I32 ea = LOAD_GPR_I32(i.def.rn);
  ea = SUB_IMM_I32(ea, 4);
  STORE_GPR_I32(i.def.rn, ea);

  /* store Rm at (Rn) */
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* MOV.B   @Rm+,Rn */
INSTR(MOVBL_INC) {
  /* store (Rm) at Rn */
  I32 ea = LOAD_GPR_I32(i.def.rm);
  I32 v = LOAD_I8(ea);
  v = SEXT_I8_I32(v);
  STORE_GPR_I32(i.def.rn, v);

  /* post-increment Rm by 1, ignore if Rm == Rn as store would overwrite */
  if (i.def.rm != i.def.rn) {
    ea = ADD_IMM_I32(ea, 1);
    STORE_GPR_I32(i.def.rm, ea);
  }

  NEXT_INSTR();
}

/* MOV.W   @Rm+,Rn */
INSTR(MOVWL_INC) {
  /* store (Rm) at Rn */
  I32 ea = LOAD_GPR_I32(i.def.rm);
  I32 v = LOAD_I16(ea);
  v = SEXT_I16_I32(v);
  STORE_GPR_I32(i.def.rn, v);

  /* post-increment Rm by 2, ignore if Rm == Rn as store would overwrite */
  if (i.def.rm != i.def.rn) {
    ea = ADD_IMM_I32(ea, 2);
    STORE_GPR_I32(i.def.rm, ea);
  }

  NEXT_INSTR();
}

/* MOV.L   @Rm+,Rn */
INSTR(MOVLL_INC) {
  /* store (Rm) at Rn */
  I32 ea = LOAD_GPR_I32(i.def.rm);
  I32 v = LOAD_I32(ea);
  STORE_GPR_I32(i.def.rn, v);

  /* post-increment Rm by 4, ignore if Rm == Rn as store would overwrite */
  if (i.def.rm != i.def.rn) {
    ea = ADD_IMM_I32(ea, 4);
    STORE_GPR_I32(i.def.rm, ea);
  }

  NEXT_INSTR();
}

/* MOV.B   R0,@(disp,Rn) */
INSTR(MOVBS_OFF) {
  I32 ea = LOAD_GPR_I32(i.def.rm);
  ea = ADD_IMM_I32(ea, (int32_t)i.def.disp);
  I8 v = LOAD_GPR_I8(0);
  STORE_I8(ea, v);
  NEXT_INSTR();
}

/* MOV.W   R0,@(disp,Rn) */
INSTR(MOVWS_OFF) {
  I32 ea = LOAD_GPR_I32(i.def.rm);
  ea = ADD_IMM_I32(ea, (int32_t)i.def.disp * 2);
  I16 v = LOAD_GPR_I16(0);
  STORE_I16(ea, v);
  NEXT_INSTR();
}

/* MOV.L Rm,@(disp,Rn) */
INSTR(MOVLS_OFF) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  ea = ADD_IMM_I32(ea, (int32_t)i.def.disp * 4);
  I32 v = LOAD_GPR_I32(i.def.rm);
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* MOV.B   @(disp,Rm),R0 */
INSTR(MOVBL_OFF) {
  I32 ea = LOAD_GPR_I32(i.def.rm);
  ea = ADD_IMM_I32(ea, (int32_t)i.def.disp);
  I32 v = LOAD_I8(ea);
  v = SEXT_I8_I32(v);
  STORE_GPR_I32(0, v);
  NEXT_INSTR();
}

/* MOV.W   @(disp,Rm),R0 */
INSTR(MOVWL_OFF) {
  I32 ea = LOAD_GPR_I32(i.def.rm);
  ea = ADD_IMM_I32(ea, (int32_t)i.def.disp * 2);
  I32 v = LOAD_I16(ea);
  v = SEXT_I16_I32(v);
  STORE_GPR_I32(0, v);
  NEXT_INSTR();
}

/* MOV.L   @(disp,Rm),Rn */
INSTR(MOVLL_OFF) {
  I32 ea = LOAD_GPR_I32(i.def.rm);
  ea = ADD_IMM_I32(ea, (int32_t)i.def.disp * 4);
  I32 v = LOAD_I32(ea);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* MOV.B   Rm,@(R0,Rn) */
INSTR(MOVBS_IDX) {
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GPR_I32(i.def.rn));
  I8 v = LOAD_GPR_I8(i.def.rm);
  STORE_I8(ea, v);
  NEXT_INSTR();
}

/* MOV.W   Rm,@(R0,Rn) */
INSTR(MOVWS_IDX) {
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GPR_I32(i.def.rn));
  I16 v = LOAD_GPR_I16(i.def.rm);
  STORE_I16(ea, v);
  NEXT_INSTR();
}

/* MOV.L   Rm,@(R0,Rn) */
INSTR(MOVLS_IDX) {
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GPR_I32(i.def.rn));
  I32 v = LOAD_GPR_I32(i.def.rm);
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* MOV.B   @(R0,Rm),Rn */
INSTR(MOVBL_IDX) {
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GPR_I32(i.def.rm));
  I32 v = LOAD_I8(ea);
  v = SEXT_I8_I32(v);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* MOV.W   @(R0,Rm),Rn */
INSTR(MOVWL_IDX) {
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GPR_I32(i.def.rm));
  I32 v = LOAD_I16(ea);
  v = SEXT_I16_I32(v);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* MOV.L   @(R0,Rm),Rn */
INSTR(MOVLL_IDX) {
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GPR_I32(i.def.rm));
  I32 v = LOAD_I32(ea);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* MOV.B   R0,@(disp,GBR) */
INSTR(MOVBS_GBR) {
  I32 ea = LOAD_GBR_I32();
  ea = ADD_IMM_I32(ea, (int32_t)i.disp_8.disp);
  I8 v = LOAD_GPR_I8(0);
  STORE_I8(ea, v);
  NEXT_INSTR();
}

/* MOV.W   R0,@(disp,GBR) */
INSTR(MOVWS_GBR) {
  I32 ea = LOAD_GBR_I32();
  ea = ADD_IMM_I32(ea, (int32_t)i.disp_8.disp * 2);
  I16 v = LOAD_GPR_I16(0);
  STORE_I16(ea, v);
  NEXT_INSTR();
}

/* MOV.L   R0,@(disp,GBR) */
INSTR(MOVLS_GBR) {
  I32 ea = LOAD_GBR_I32();
  ea = ADD_IMM_I32(ea, (int32_t)i.disp_8.disp * 4);
  I32 v = LOAD_GPR_I32(0);
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* MOV.B   @(disp,GBR),R0 */
INSTR(MOVBL_GBR) {
  I32 ea = LOAD_GBR_I32();
  ea = ADD_IMM_I32(ea, (int32_t)i.disp_8.disp);
  I32 v = LOAD_I8(ea);
  v = SEXT_I8_I32(v);
  STORE_GPR_I32(0, v);
  NEXT_INSTR();
}

/* MOV.W   @(disp,GBR),R0 */
INSTR(MOVWL_GBR) {
  I32 ea = LOAD_GBR_I32();
  ea = ADD_IMM_I32(ea, (int32_t)i.disp_8.disp * 2);
  I32 v = LOAD_I16(ea);
  v = SEXT_I16_I32(v);
  STORE_GPR_I32(0, v);
  NEXT_INSTR();
}

/* MOV.L   @(disp,GBR),R0 */
INSTR(MOVLL_GBR) {
  I32 ea = LOAD_GBR_I32();
  ea = ADD_IMM_I32(ea, (int32_t)i.disp_8.disp * 4);
  I32 v = LOAD_I32(ea);
  STORE_GPR_I32(0, v);
  NEXT_INSTR();
}

/* MOVA    (disp,PC),R0 */
INSTR(MOVA) {
  uint32_t ea = (i.disp_8.disp * 4) + (addr & ~3) + 4;
  STORE_GPR_IMM_I32(0, ea);
  NEXT_INSTR();
}

/* MOVT    Rn */
INSTR(MOVT) {
  I32 v = LOAD_T_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* SWAP.B  Rm,Rn */
INSTR(SWAPB) {
  const int nbits = 8;
  const uint32_t mask = (1u << nbits) - 1;
  I32 v = LOAD_GPR_I32(i.def.rm);
  I32 tmp = AND_IMM_I32(XOR_I32(v, LSHR_IMM_I32(v, nbits)), mask);
  I32 res = XOR_I32(v, OR_I32(tmp, SHL_IMM_I32(tmp, nbits)));
  STORE_GPR_I32(i.def.rn, res);
  NEXT_INSTR();
}

/* SWAP.W  Rm,Rn */
INSTR(SWAPW) {
  const int nbits = 16;
  const uint32_t mask = (1u << nbits) - 1;
  I32 v = LOAD_GPR_I32(i.def.rm);
  I32 tmp = AND_IMM_I32(XOR_I32(v, LSHR_IMM_I32(v, nbits)), mask);
  I32 res = XOR_I32(v, OR_I32(tmp, SHL_IMM_I32(tmp, nbits)));
  STORE_GPR_I32(i.def.rn, res);
  NEXT_INSTR();
}

/* XTRCT   Rm,Rn */
INSTR(XTRCT) {
  I32 rm = SHL_IMM_I32(AND_IMM_I32(LOAD_GPR_I32(i.def.rm), 0x0000ffff), 16);
  I32 rn = LSHR_IMM_I32(AND_IMM_I32(LOAD_GPR_I32(i.def.rn), 0xffff0000), 16);
  I32 v = OR_I32(rm, rn);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* ADD     Rm,Rn */
INSTR(ADD) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 v = ADD_I32(rn, rm);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* ADD     #imm,Rn */
INSTR(ADDI) {
  I32 rn = LOAD_GPR_I32(i.imm.rn);
  /* sign extend mask */
  int32_t mask = (int32_t)(int8_t)i.imm.imm;
  I32 v = ADD_IMM_I32(rn, mask);
  STORE_GPR_I32(i.imm.rn, v);
  NEXT_INSTR();
}

/* ADDC    Rm,Rn */
INSTR(ADDC) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 v = ADD_I32(ADD_I32(rn, rm), LOAD_T_I32());
  STORE_GPR_I32(i.def.rn, v);

  /* compute carry flag, taken from Hacker's Delight */
  I32 and_rnrm = AND_I32(rn, rm);
  I32 or_rnrm = OR_I32(rn, rm);
  I32 not_v = NOT_I32(v);
  I32 carry = AND_I32(or_rnrm, not_v);
  carry = OR_I32(carry, and_rnrm);
  carry = LSHR_IMM_I32(carry, 31);
  STORE_T_I32(carry);
  NEXT_INSTR();
}

/* ADDV    Rm,Rn */
INSTR(ADDV) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 v = ADD_I32(rn, rm);
  STORE_GPR_I32(i.def.rn, v);

  /* compute overflow flag, taken from Hacker's Delight */
  I32 overflow = LSHR_IMM_I32(AND_I32(XOR_I32(v, rn), XOR_I32(v, rm)), 31);
  STORE_T_I32(overflow);
  NEXT_INSTR();
}

/* CMP/EQ #imm,R0 */
INSTR(CMPEQI) {
  int32_t imm = (int32_t)(int8_t)i.imm.imm;
  I32 r0 = LOAD_GPR_I32(0);
  I8 eq = CMPEQ_IMM_I32(r0, imm);
  STORE_T_I8(eq);
  NEXT_INSTR();
}

/* CMP/EQ  Rm,Rn */
INSTR(CMPEQ) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I8 eq = CMPEQ_I32(rn, rm);
  STORE_T_I8(eq);
  NEXT_INSTR();
}

/* CMP/HS  Rm,Rn */
INSTR(CMPHS) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I8 r = CMPUGE_I32(rn, rm);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* CMP/GE  Rm,Rn */
INSTR(CMPGE) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I8 r = CMPSGE_I32(rn, rm);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* CMP/HI  Rm,Rn */
INSTR(CMPHI) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I8 r = CMPUGT_I32(rn, rm);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* CMP/GT  Rm,Rn */
INSTR(CMPGT) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I8 r = CMPSGT_I32(rn, rm);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* CMP/PZ  Rn */
INSTR(CMPPZ) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I8 r = CMPSGE_IMM_I32(rn, 0);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* CMP/PL  Rn */
INSTR(CMPPL) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I8 r = CMPSGT_IMM_I32(rn, 0);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* CMP/STR  Rm,Rn */
INSTR(CMPSTR) {
  /* if any diff is zero, the bytes match */
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 diff = XOR_I32(rn, rm);
  I8 b4_eq = CMPEQ_IMM_I32(AND_IMM_I32(diff, 0xff000000), 0);
  I8 b3_eq = CMPEQ_IMM_I32(AND_IMM_I32(diff, 0x00ff0000), 0);
  I8 b2_eq = CMPEQ_IMM_I32(AND_IMM_I32(diff, 0x0000ff00), 0);
  I8 b1_eq = CMPEQ_IMM_I32(AND_IMM_I32(diff, 0x000000ff), 0);
  I8 r = OR_I8(OR_I8(OR_I8(b1_eq, b2_eq), b3_eq), b4_eq);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* DIV0S   Rm,Rn */
INSTR(DIV0S) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 qnotm = XOR_I32(rn, rm);

  /* save off M */
  I32 m = LSHR_IMM_I32(rm, 31);
  STORE_M_I32(m);

  /* update Q == M flag */
  I32 qm = NOT_I32(qnotm);
  STORE_QM_I32(qm);

  /* msb of Q ^ M -> T */
  I32 r = LSHR_IMM_I32(qnotm, 31);
  STORE_T_I32(r);
  NEXT_INSTR();
}

/* DIV0U */
INSTR(DIV0U) {
  STORE_M_IMM_I32(0);
  STORE_QM_IMM_I32((int32_t)0x80000000);
  STORE_T_IMM_I32(0);
  NEXT_INSTR();
}

/* DIV1 Rm,Rn */
INSTR(DIV1) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);

  /* if Q == M, r0 = ~Rm and C = 1; else, r0 = Rm and C = 0 */
  I32 qm = ASHR_IMM_I32(LOAD_QM_I32(), 31);
  I32 r0 = XOR_I32(rm, qm);
  I32 carry = LSHR_IMM_I32(qm, 31);

  /* initialize output bit as (Q == M) ^ Rn */
  qm = XOR_I32(qm, rn);

  /* shift Rn left by 1 and add T */
  rn = SHL_IMM_I32(rn, 1);
  rn = OR_I32(rn, LOAD_T_I32());

  /* add or subtract Rm based on r0 and C */
  I32 rd = ADD_I32(rn, r0);
  rd = ADD_I32(rd, carry);
  STORE_GPR_I32(i.def.rn, rd);

  /* if C is cleared, invert output bit */
  I32 and_rnr0 = AND_I32(rn, r0);
  I32 or_rnr0 = OR_I32(rn, r0);
  I32 not_rd = NOT_I32(rd);
  carry = AND_I32(or_rnr0, not_rd);
  carry = OR_I32(and_rnr0, carry);
  carry = LSHR_IMM_I32(carry, 31);
  qm = SELECT_I32(carry, qm, NOT_I32(qm));
  STORE_QM_I32(qm);

  /* set T to output bit (which happens to be Q == M) */
  I32 t = LSHR_IMM_I32(qm, 31);
  STORE_T_I32(t);

  NEXT_INSTR();
}

/* DMULS.L Rm,Rn */
INSTR(DMULS) {
  I64 rm = SEXT_I32_I64(LOAD_GPR_I32(i.def.rm));
  I64 rn = SEXT_I32_I64(LOAD_GPR_I32(i.def.rn));
  I64 p = SMUL_I64(rm, rn);
  I32 lo = TRUNC_I64_I32(p);
  I32 hi = TRUNC_I64_I32(LSHR_IMM_I64(p, 32));
  STORE_MACL_I32(lo);
  STORE_MACH_I32(hi);
  NEXT_INSTR();
}

/* DMULU.L Rm,Rn */
INSTR(DMULU) {
  I64 rm = ZEXT_I32_I64(LOAD_GPR_I32(i.def.rm));
  I64 rn = ZEXT_I32_I64(LOAD_GPR_I32(i.def.rn));
  I64 p = UMUL_I64(rm, rn);
  I32 lo = TRUNC_I64_I32(p);
  I32 hi = TRUNC_I64_I32(LSHR_IMM_I64(p, 32));
  STORE_MACL_I32(lo);
  STORE_MACH_I32(hi);
  NEXT_INSTR();
}

/* DT      Rn */
INSTR(DT) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  v = SUB_IMM_I32(v, 1);
  STORE_GPR_I32(i.def.rn, v);
  I8 r = CMPEQ_IMM_I32(v, 0);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* EXTS.B  Rm,Rn */
INSTR(EXTSB) {
  I32 v = LOAD_GPR_I8(i.def.rm);
  v = SEXT_I8_I32(v);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* EXTS.W  Rm,Rn */
INSTR(EXTSW) {
  I32 v = LOAD_GPR_I16(i.def.rm);
  v = SEXT_I16_I32(v);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* EXTU.B  Rm,Rn */
INSTR(EXTUB) {
  I32 v = LOAD_GPR_I8(i.def.rm);
  v = ZEXT_I8_I32(v);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* EXTU.W  Rm,Rn */
INSTR(EXTUW) {
  I32 v = LOAD_GPR_I16(i.def.rm);
  v = ZEXT_I16_I32(v);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* MAC.L   @Rm+,@Rn+ */
INSTR(MACL) {
  LOG_FATAL("MACL not implemented");
}

/* MAC.W   @Rm+,@Rn+ */
INSTR(MACW) {
  LOG_FATAL("MACW not implemented");
}

/* MUL.L   Rm,Rn */
INSTR(MULL) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 v = SMUL_I32(rn, rm);
  STORE_MACL_I32(v);
  NEXT_INSTR();
}

/* MULS    Rm,Rn */
INSTR(MULS) {
  I32 rm = SEXT_I16_I32(LOAD_GPR_I16(i.def.rm));
  I32 rn = SEXT_I16_I32(LOAD_GPR_I16(i.def.rn));
  I32 v = SMUL_I32(rn, rm);
  STORE_MACL_I32(v);
  NEXT_INSTR();
}

/* MULU    Rm,Rn */
INSTR(MULU) {
  I32 rm = ZEXT_I16_I32(LOAD_GPR_I16(i.def.rm));
  I32 rn = ZEXT_I16_I32(LOAD_GPR_I16(i.def.rn));
  I32 v = UMUL_I32(rn, rm);
  STORE_MACL_I32(v);
  NEXT_INSTR();
}

/* NEG     Rm,Rn */
INSTR(NEG) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 v = NEG_I32(rm);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* NEGC    Rm,Rn */
INSTR(NEGC) {
  I32 t = LOAD_T_I32();
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 v = SUB_I32(NEG_I32(rm), t);
  STORE_GPR_I32(i.def.rn, v);

  /* compute carry flag, taken from Hacker's Delight */
  I32 carry = LSHR_IMM_I32(OR_I32(rm, v), 31);
  STORE_T_I32(carry);

  NEXT_INSTR();
}

/* SUB     Rm,Rn */
INSTR(SUB) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 v = SUB_I32(rn, rm);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* SUBC    Rm,Rn */
INSTR(SUBC) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 v = SUB_I32(rn, rm);
  v = SUB_I32(v, LOAD_T_I32());
  STORE_GPR_I32(i.def.rn, v);

  /* compute carry flag, taken from Hacker's Delight */
  I32 not_rn = NOT_I32(rn);
  I32 l = AND_I32(not_rn, rm);
  I32 r = AND_I32(OR_I32(not_rn, rm), v);
  I32 carry = LSHR_IMM_I32(OR_I32(l, r), 31);
  STORE_T_I32(carry);

  NEXT_INSTR();
}

/* SUBV    Rm,Rn */
INSTR(SUBV) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 v = SUB_I32(rn, rm);
  STORE_GPR_I32(i.def.rn, v);

  /* compute overflow flag, taken from Hacker's Delight */
  I32 o = LSHR_IMM_I32(AND_I32(XOR_I32(rn, rm), XOR_I32(v, rn)), 31);
  STORE_T_I32(o);
  NEXT_INSTR();
}

/* AND     Rm,Rn */
INSTR(AND) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 v = AND_I32(rn, rm);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* AND     #imm,R0 */
INSTR(ANDI) {
  uint32_t mask = i.imm.imm;
  I32 v = LOAD_GPR_I32(0);
  v = AND_IMM_I32(v, mask);
  STORE_GPR_I32(0, v);
  NEXT_INSTR();
}

/* AND.B   #imm,@(R0,GBR) */
INSTR(ANDB) {
  uint8_t mask = (uint8_t)i.imm.imm;
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GBR_I32());
  I8 v = LOAD_I8(ea);
  v = AND_IMM_I8(v, mask);
  STORE_I8(ea, v);
  NEXT_INSTR();
}

/* NOT     Rm,Rn */
INSTR(NOT) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 v = NOT_I32(rm);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* OR      Rm,Rn */
INSTR(OR) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 v = OR_I32(rn, rm);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* OR      #imm,R0 */
INSTR(ORI) {
  uint32_t mask = i.imm.imm;
  I32 v = LOAD_GPR_I32(0);
  v = OR_IMM_I32(v, mask);
  STORE_GPR_I32(0, v);
  NEXT_INSTR();
}

/* OR.B    #imm,@(R0,GBR) */
INSTR(ORB) {
  uint8_t mask = (uint8_t)i.imm.imm;
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GBR_I32());
  I8 v = LOAD_I8(ea);
  v = OR_IMM_I8(v, mask);
  STORE_I8(ea, v);
  NEXT_INSTR();
}

/* TAS.B   @Rn */
INSTR(TAS) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I8 v = LOAD_I8(ea);
  STORE_I8(ea, OR_IMM_I8(v, (uint8_t)0x80));
  I8 r = CMPEQ_IMM_I8(v, 0);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* TST     Rm,Rn */
INSTR(TST) {
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I8 r = CMPEQ_IMM_I32(AND_I32(rn, rm), 0);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* TST     #imm,R0 */
INSTR(TSTI) {
  uint32_t mask = i.imm.imm;
  I32 r0 = LOAD_GPR_I32(0);
  I8 r = CMPEQ_IMM_I32(AND_IMM_I32(r0, mask), 0);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* TST.B   #imm,@(R0,GBR) */
INSTR(TSTB) {
  uint8_t mask = i.imm.imm;
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GBR_I32());
  I8 v = LOAD_I8(ea);
  I8 r = CMPEQ_IMM_I8(AND_IMM_I8(v, mask), 0);
  STORE_T_I8(r);
  NEXT_INSTR();
}

/* XOR     Rm,Rn */
INSTR(XOR) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rm = LOAD_GPR_I32(i.def.rm);
  I32 v = XOR_I32(rn, rm);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* XOR     #imm,R0 */
INSTR(XORI) {
  uint32_t mask = (uint32_t)i.imm.imm;
  I32 v = LOAD_GPR_I32(0);
  v = XOR_IMM_I32(v, mask);
  STORE_GPR_I32(0, v);
  NEXT_INSTR();
}

/* XOR.B   #imm,@(R0,GBR) */
INSTR(XORB) {
  uint8_t mask = (uint8_t)i.imm.imm;
  I32 ea = LOAD_GPR_I32(0);
  ea = ADD_I32(ea, LOAD_GBR_I32());
  I8 v = LOAD_I8(ea);
  v = XOR_IMM_I8(v, mask);
  STORE_I8(ea, v);
  NEXT_INSTR();
}

/* ROTL    Rn */
INSTR(ROTL) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rn_msb = AND_IMM_I32(LSHR_IMM_I32(rn, 31), 0x1);
  I32 v = OR_I32(SHL_IMM_I32(rn, 1), rn_msb);
  STORE_GPR_I32(i.def.rn, v);
  STORE_T_I32(rn_msb);
  NEXT_INSTR();
}

/* ROTR    Rn */
INSTR(ROTR) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rn_lsb = AND_IMM_I32(rn, 0x1);
  I32 v = OR_I32(SHL_IMM_I32(rn_lsb, 31), LSHR_IMM_I32(rn, 1));
  STORE_GPR_I32(i.def.rn, v);
  STORE_T_I32(rn_lsb);
  NEXT_INSTR();
}

/* ROTCL   Rn */
INSTR(ROTCL) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rn_msb = AND_IMM_I32(LSHR_IMM_I32(rn, 31), 0x1);
  I32 v = OR_I32(SHL_IMM_I32(rn, 1), LOAD_T_I32());
  STORE_GPR_I32(i.def.rn, v);
  STORE_T_I32(rn_msb);
  NEXT_INSTR();
}

/* ROTCR   Rn */
INSTR(ROTCR) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rn_lsb = AND_IMM_I32(rn, 0x1);
  I32 v = OR_I32(SHL_IMM_I32(LOAD_T_I32(), 31), LSHR_IMM_I32(rn, 1));
  STORE_GPR_I32(i.def.rn, v);
  STORE_T_I32(rn_lsb);
  NEXT_INSTR();
}

/* SHAD    Rm,Rn */
INSTR(SHAD) {
  /*
  * when Rm >= 0, Rn << Rm
  * when Rm < 0, Rn >> Rm
  * when shifting right > 32, Rn = (Rn >= 0 ? 0 : -1)
  */
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rm = LOAD_GPR_I32(i.def.rm);
  STORE_GPR_I32(i.def.rn, ASHD_I32(rn, rm));
  NEXT_INSTR();
}

/* SHAL    Rn      (same as SHLL) */
INSTR(SHAL) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rn_msb = AND_IMM_I32(LSHR_IMM_I32(rn, 31), 0x1);
  I32 v = SHL_IMM_I32(rn, 1);
  STORE_GPR_I32(i.def.rn, v);
  STORE_T_I32(rn_msb);
  NEXT_INSTR();
}

/* SHAR    Rn */
INSTR(SHAR) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rn_lsb = AND_IMM_I32(rn, 0x1);
  I32 v = ASHR_IMM_I32(rn, 1);
  STORE_GPR_I32(i.def.rn, v);
  STORE_T_I32(rn_lsb);
  NEXT_INSTR();
}

/* SHLD    Rm,Rn */
INSTR(SHLD) {
  /*
  * when Rm >= 0, Rn << Rm
  * when Rm < 0, Rn >> Rm
  * when shifting right >= 32, Rn = 0
  */
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rm = LOAD_GPR_I32(i.def.rm);
  STORE_GPR_I32(i.def.rn, LSHD_I32(rn, rm));
  NEXT_INSTR();
}

/* SHLL    Rn      (same as SHAL) */
INSTR(SHLL) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rn_msb = AND_IMM_I32(LSHR_IMM_I32(rn, 31), 1);
  STORE_GPR_I32(i.def.rn, SHL_IMM_I32(rn, 1));
  STORE_T_I32(rn_msb);
  NEXT_INSTR();
}

/* SHLR    Rn */
INSTR(SHLR) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 rn_lsb = AND_IMM_I32(rn, 0x1);
  STORE_GPR_I32(i.def.rn, LSHR_IMM_I32(rn, 1));
  STORE_T_I32(rn_lsb);
  NEXT_INSTR();
}

/* SHLL2   Rn */
INSTR(SHLL2) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  STORE_GPR_I32(i.def.rn, SHL_IMM_I32(rn, 2));
  NEXT_INSTR();
}

/* SHLR2   Rn */
INSTR(SHLR2) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  STORE_GPR_I32(i.def.rn, LSHR_IMM_I32(rn, 2));
  NEXT_INSTR();
}

/* SHLL8   Rn */
INSTR(SHLL8) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  STORE_GPR_I32(i.def.rn, SHL_IMM_I32(rn, 8));
  NEXT_INSTR();
}

/* SHLR8   Rn */
INSTR(SHLR8) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  STORE_GPR_I32(i.def.rn, LSHR_IMM_I32(rn, 8));
  NEXT_INSTR();
}

/* SHLL16  Rn */
INSTR(SHLL16) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  STORE_GPR_I32(i.def.rn, SHL_IMM_I32(rn, 16));
  NEXT_INSTR();
}

/* SHLR16  Rn */
INSTR(SHLR16) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  STORE_GPR_I32(i.def.rn, LSHR_IMM_I32(rn, 16));
  NEXT_INSTR();
}

/* BF      disp */
INSTR(BF) {
  I32 cond = LOAD_T_I32();
  uint32_t dest_addr = ((int8_t)i.disp_8.disp * 2) + addr + 4;
  BRANCH_COND_IMM_I32(cond, addr + 2, dest_addr);
}

/* BFS     disp */
INSTR(BFS) {
  I32 cond = LOAD_T_I32();
  uint32_t dest_addr = ((int8_t)i.disp_8.disp * 2) + addr + 4;
  DELAY_INSTR();
  BRANCH_COND_IMM_I32(cond, addr + 4, dest_addr);
}

/* BT      disp */
INSTR(BT) {
  I32 cond = LOAD_T_I32();
  uint32_t dest_addr = ((int8_t)i.disp_8.disp * 2) + addr + 4;
  BRANCH_COND_IMM_I32(cond, dest_addr, addr + 2);
}

/* BTS     disp */
INSTR(BTS) {
  /* 8-bit displacement must be sign extended */
  uint32_t dest_addr = ((int8_t)i.disp_8.disp * 2) + addr + 4;
  I32 cond = LOAD_T_I32();
  DELAY_INSTR();
  BRANCH_COND_IMM_I32(cond, dest_addr, addr + 4);
}

/* BRA     disp */
INSTR(BRA) {
  /* 12-bit displacement must be sign extended */
  int32_t disp = (((int32_t)i.disp_12.disp & 0xfff) << 20) >> 20;
  uint32_t dest_addr = (disp * 2) + addr + 4;
  DELAY_INSTR();
  BRANCH_IMM_I32(dest_addr);
}

/* BRAF    Rn */
INSTR(BRAF) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  I32 dest_addr = ADD_IMM_I32(rn, addr + 4);
  DELAY_INSTR();
  BRANCH_I32(dest_addr);
}

/* BSR     disp */
INSTR(BSR) {
  /* 12-bit displacement must be sign extended */
  int32_t disp = (((int32_t)i.disp_12.disp & 0xfff) << 20) >> 20;
  uint32_t ret_addr = addr + 4;
  uint32_t dest_addr = ret_addr + disp * 2;
  DELAY_INSTR();
  STORE_PR_IMM_I32(ret_addr);
  BRANCH_IMM_I32(dest_addr);
}

/* BSRF    Rn */
INSTR(BSRF) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  uint32_t ret_addr = addr + 4;
  I32 dest_addr = ADD_IMM_I32(rn, ret_addr);
  DELAY_INSTR();
  STORE_PR_IMM_I32(ret_addr);
  BRANCH_I32(dest_addr);
}

/* JMP     @Rn */
INSTR(JMP) {
  I32 dest_addr = LOAD_GPR_I32(i.def.rn);
  DELAY_INSTR();
  BRANCH_I32(dest_addr);
}

/* JSR     @Rn */
INSTR(JSR) {
  I32 dest_addr = LOAD_GPR_I32(i.def.rn);
  uint32_t ret_addr = addr + 4;
  DELAY_INSTR();
  STORE_PR_IMM_I32(ret_addr);
  BRANCH_I32(dest_addr);
}

/* RTS */
INSTR(RTS) {
  I32 dest_addr = LOAD_PR_I32();
  DELAY_INSTR();
  BRANCH_I32(dest_addr);
}

/* CLRMAC */
INSTR(CLRMAC) {
  STORE_MACH_IMM_I32(0);
  STORE_MACL_IMM_I32(0);
  NEXT_INSTR();
}

/* CLRS */
INSTR(CLRS) {
  STORE_S_IMM_I32(0);
  NEXT_INSTR();
}

/* CLRT */
INSTR(CLRT) {
  STORE_T_IMM_I32(0);
  NEXT_INSTR();
}

/* LDC     Rm,SR */
INSTR(LDCSR) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_SR_I32(v);
  NEXT_INSTR();
}

/* LDC     Rm,GBR */
INSTR(LDCGBR) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_GBR_I32(v);
  NEXT_INSTR();
}

/* LDC     Rm,VBR */
INSTR(LDCVBR) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_VBR_I32(v);
  NEXT_INSTR();
}

/* LDC     Rm,SSR */
INSTR(LDCSSR) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_SSR_I32(v);
  NEXT_INSTR();
}

/* LDC     Rm,SPC */
INSTR(LDCSPC) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_SPC_I32(v);
  NEXT_INSTR();
}

/* LDC     Rm,DBR */
INSTR(LDCDBR) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_DBR_I32(v);
  NEXT_INSTR();
}

/* LDC.L   Rm,Rn_BANK */
INSTR(LDCRBANK) {
  int reg = i.def.rm & 0x7;
  I32 rn = LOAD_GPR_I32(i.def.rn);
  STORE_GPR_ALT_I32(reg, rn);
  NEXT_INSTR();
}

/* LDC.L   @Rm+,SR */
INSTR(LDCMSR) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_SR_I32(v);
  /* reload Rn, sr store could have swapped banks */
  ea = LOAD_GPR_I32(i.def.rn);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  NEXT_INSTR();
}

/* LDC.L   @Rm+,GBR */
INSTR(LDCMGBR) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_GBR_I32(v);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  NEXT_INSTR();
}

/* LDC.L   @Rm+,VBR */
INSTR(LDCMVBR) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_VBR_I32(v);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  NEXT_INSTR();
}

/* LDC.L   @Rm+,SSR */
INSTR(LDCMSSR) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_SSR_I32(v);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  NEXT_INSTR();
}

/* LDC.L   @Rm+,SPC */
INSTR(LDCMSPC) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_SPC_I32(v);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  NEXT_INSTR();
}

/* LDC.L   @Rm+,DBR */
INSTR(LDCMDBR) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_DBR_I32(v);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  NEXT_INSTR();
}

/* LDC.L   @Rm+,Rn_BANK */
INSTR(LDCMRBANK) {
  int reg = i.def.rm & 0x7;
  I32 ea = LOAD_GPR_I32(i.def.rn);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  I32 v = LOAD_I32(ea);
  STORE_GPR_ALT_I32(reg, v);
  NEXT_INSTR();
}

/* LDS     Rm,MACH */
INSTR(LDSMACH) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_MACH_I32(v);
  NEXT_INSTR();
}

/* LDS     Rm,MACL */
INSTR(LDSMACL) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_MACL_I32(v);
  NEXT_INSTR();
}

/* LDS     Rm,PR */
INSTR(LDSPR) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_PR_I32(v);
  NEXT_INSTR();
}

/* LDS.L   @Rm+,MACH */
INSTR(LDSMMACH) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_MACH_I32(v);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  NEXT_INSTR();
}

/* LDS.L   @Rm+,MACL */
INSTR(LDSMMACL) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_MACL_I32(v);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  NEXT_INSTR();
}

/* LDS.L   @Rm+,PR */
INSTR(LDSMPR) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_PR_I32(v);
  STORE_GPR_I32(i.def.rn, ADD_IMM_I32(ea, 4));
  NEXT_INSTR();
}

/* LDTLB */
INSTR(LDTLB) {
  LDTLB();
}

/* MOVCA.L     R0,@Rn */
INSTR(MOVCAL) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_GPR_I32(0);
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* NOP */
INSTR(NOP) {
  NEXT_INSTR();
}

/* OCBI */
INSTR(OCBI) {
  NEXT_INSTR();
}

/* OCBP */
INSTR(OCBP) {
  NEXT_INSTR();
}

/* OCBWB */
INSTR(OCBWB) {
  NEXT_INSTR();
}

/* PREF     @Rn */
INSTR(PREF) {
  /* check that the address is between 0xe0000000 and 0xe3ffffff */
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 cond = CMPEQ_IMM_I32(LSHR_IMM_I32(ea, 26), 0x38);
  PREF_COND(cond, ea);
  NEXT_INSTR();
}

/* RTE */
INSTR(RTE) {
  I32 ssr = LOAD_SSR_I32();
  I32 spc = LOAD_SPC_I32();

  /* in an RTE delay slot, status register bits are referenced as follows. for
     instruction access, the MD bit is used before modification, and for data
     access, the MD bit is accessed after modification. for the instruction
     execution, the other bits (S, T, M, Q, FD, BL, and RB) are used after
     modification. the STC and STC.L SR instructions access all SR bits after
     modification

     note, since the MD bit isn't actually emulated, the SR is just set before
     executing the delay slot */
  STORE_SR_I32(ssr);
  DELAY_INSTR();

  BRANCH_I32(spc);
}

/* SETS */
INSTR(SETS) {
  STORE_S_IMM_I32(1);
  NEXT_INSTR();
}

/* SETT */
INSTR(SETT) {
  STORE_T_IMM_I32(1);
  NEXT_INSTR();
}

/* SLEEP */
INSTR(SLEEP) {
  SLEEP();
}

/* STC     SR,Rn */
INSTR(STCSR) {
  I32 v = LOAD_SR_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STC     GBR,Rn */
INSTR(STCGBR) {
  I32 v = LOAD_GBR_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STC     VBR,Rn */
INSTR(STCVBR) {
  I32 v = LOAD_VBR_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STC     SSR,Rn */
INSTR(STCSSR) {
  I32 v = LOAD_SSR_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STC     SPC,Rn */
INSTR(STCSPC) {
  I32 v = LOAD_SPC_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STC     SGR,Rn */
INSTR(STCSGR) {
  I32 v = LOAD_SGR_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STC     DBR,Rn */
INSTR(STCDBR) {
  I32 v = LOAD_DBR_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STC     Rm_BANK,Rn */
INSTR(STCRBANK) {
  int reg = i.def.rm & 0x7;
  I32 v = LOAD_GPR_ALT_I32(reg);
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STC.L   SR,@-Rn */
INSTR(STCMSR) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_SR_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STC.L   GBR,@-Rn */
INSTR(STCMGBR) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_GBR_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STC.L   VBR,@-Rn */
INSTR(STCMVBR) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_VBR_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STC.L   SSR,@-Rn */
INSTR(STCMSSR) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_SSR_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STC.L   SPC,@-Rn */
INSTR(STCMSPC) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_SPC_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STC.L   SGR,@-Rn */
INSTR(STCMSGR) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_SGR_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STC.L   DBR,@-Rn */
INSTR(STCMDBR) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_DBR_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STC.L   Rm_BANK,@-Rn */
INSTR(STCMRBANK) {
  int reg = i.def.rm & 0x7;
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_GPR_ALT_I32(reg);
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STS     MACH,Rn */
INSTR(STSMACH) {
  I32 v = LOAD_MACH_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STS     MACL,Rn */
INSTR(STSMACL) {
  I32 v = LOAD_MACL_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STS     PR,Rn */
INSTR(STSPR) {
  I32 v = LOAD_PR_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STS.L   MACH,@-Rn */
INSTR(STSMMACH) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_MACH_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STS.L   MACL,@-Rn */
INSTR(STSMMACL) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_MACL_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STS.L   PR,@-Rn */
INSTR(STSMPR) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_PR_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* TRAPA   #imm */
INSTR(TRAPA) {
  LOG_FATAL("unsupported TRAPA");
}

/* FLDI0  FRn 1111nnnn10001101 */
INSTR(FLDI0) {
  STORE_FPR_IMM_I32(i.def.rn, 0x00000000);
  NEXT_INSTR();
}

/* FLDI1  FRn 1111nnnn10011101 */
INSTR(FLDI1) {
  STORE_FPR_IMM_I32(i.def.rn, 0x3f800000);
  NEXT_INSTR();
}

/* FMOV    FRm,FRn 1111nnnnmmmm1100
   FMOV    DRm,DRn 1111nnn0mmm01100
   FMOV    XDm,DRn 1111nnn0mmm11100
   FMOV    DRm,XDn 1111nnn1mmm01100
   FMOV    XDm,XDn 1111nnn1mmm11100 */
INSTR(FMOV) {
  if (FPU_DOUBLE_SZ) {
    if (i.def.rm & 1) {
      I64 rm = LOAD_XFR_I64(i.def.rm & 0xe);
      if (i.def.rn & 1) {
        STORE_XFR_I64(i.def.rn & 0xe, rm);
      } else {
        STORE_FPR_I64(i.def.rn, rm);
      }
    } else {
      I64 rm = LOAD_FPR_I64(i.def.rm);
      if (i.def.rn & 1) {
        STORE_XFR_I64(i.def.rn & 0xe, rm);
      } else {
        STORE_FPR_I64(i.def.rn, rm);
      }
    }
  } else {
    I32 rm = LOAD_FPR_I32(i.def.rm);
    STORE_FPR_I32(i.def.rn, rm);
  }

  NEXT_INSTR();
}

/* FMOV.S  @Rm,FRn 1111nnnnmmmm1000
   FMOV    @Rm,DRn 1111nnn0mmmm1000
   FMOV    @Rm,XDn 1111nnn1mmmm1000 */
INSTR(FMOV_LOAD) {
  I32 ea = LOAD_GPR_I32(i.def.rm);

  if (FPU_DOUBLE_SZ) {
    I32 v_lo = LOAD_I32(ea);
    I32 v_hi = LOAD_I32(ADD_IMM_I32(ea, 4));
    if (i.def.rn & 1) {
      STORE_XFR_I32(i.def.rn & 0xe, v_lo);
      STORE_XFR_I32(i.def.rn, v_hi);
    } else {
      STORE_FPR_I32(i.def.rn, v_lo);
      STORE_FPR_I32(i.def.rn | 0x1, v_hi);
    }
  } else {
    I32 v = LOAD_I32(ea);
    STORE_FPR_I32(i.def.rn, v);
  }

  NEXT_INSTR();
}

/* FMOV.S  @(R0,Rm),FRn 1111nnnnmmmm0110
   FMOV    @(R0,Rm),DRn 1111nnn0mmmm0110
   FMOV    @(R0,Rm),XDn 1111nnn1mmmm0110 */
INSTR(FMOV_INDEX_LOAD) {
  I32 ea = ADD_I32(LOAD_GPR_I32(0), LOAD_GPR_I32(i.def.rm));

  if (FPU_DOUBLE_SZ) {
    I32 v_lo = LOAD_I32(ea);
    I32 v_hi = LOAD_I32(ADD_IMM_I32(ea, 4));
    if (i.def.rn & 1) {
      STORE_XFR_I32(i.def.rn & 0xe, v_lo);
      STORE_XFR_I32(i.def.rn, v_hi);
    } else {
      STORE_FPR_I32(i.def.rn, v_lo);
      STORE_FPR_I32(i.def.rn | 0x1, v_hi);
    }
  } else {
    I32 v = LOAD_I32(ea);
    STORE_FPR_I32(i.def.rn, v);
  }

  NEXT_INSTR();
}

/* FMOV.S  FRm,@Rn 1111nnnnmmmm1010
   FMOV    DRm,@Rn 1111nnnnmmm01010
   FMOV    XDm,@Rn 1111nnnnmmm11010 */
INSTR(FMOV_STORE) {
  I32 ea = LOAD_GPR_I32(i.def.rn);

  if (FPU_DOUBLE_SZ) {
    I32 ea_lo = ea;
    I32 ea_hi = ADD_IMM_I32(ea, 4);
    if (i.def.rm & 1) {
      STORE_I32(ea_lo, LOAD_XFR_I32(i.def.rm & 0xe));
      STORE_I32(ea_hi, LOAD_XFR_I32(i.def.rm));
    } else {
      STORE_I32(ea_lo, LOAD_FPR_I32(i.def.rm));
      STORE_I32(ea_hi, LOAD_FPR_I32(i.def.rm | 0x1));
    }
  } else {
    I32 v = LOAD_FPR_I32(i.def.rm);
    STORE_I32(ea, v);
  }

  NEXT_INSTR();
}

/* FMOV.S  FRm,@(R0,Rn) 1111nnnnmmmm0111
   FMOV    DRm,@(R0,Rn) 1111nnnnmmm00111
   FMOV    XDm,@(R0,Rn) 1111nnnnmmm10111 */
INSTR(FMOV_INDEX_STORE) {
  I32 ea = ADD_I32(LOAD_GPR_I32(0), LOAD_GPR_I32(i.def.rn));

  if (FPU_DOUBLE_SZ) {
    I32 ea_lo = ea;
    I32 ea_hi = ADD_IMM_I32(ea, 4);
    if (i.def.rm & 1) {
      STORE_I32(ea_lo, LOAD_XFR_I32(i.def.rm & 0xe));
      STORE_I32(ea_hi, LOAD_XFR_I32(i.def.rm));
    } else {
      STORE_I32(ea_lo, LOAD_FPR_I32(i.def.rm));
      STORE_I32(ea_hi, LOAD_FPR_I32(i.def.rm | 0x1));
    }
  } else {
    I32 v = LOAD_FPR_I32(i.def.rm);
    STORE_I32(ea, v);
  }

  NEXT_INSTR();
}

/* FMOV.S  FRm,@-Rn 1111nnnnmmmm1011
   FMOV    DRm,@-Rn 1111nnnnmmm01011
   FMOV    XDm,@-Rn 1111nnnnmmm11011 */
INSTR(FMOV_SAVE) {
  if (FPU_DOUBLE_SZ) {
    I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 8);
    STORE_GPR_I32(i.def.rn, ea);

    I32 ea_lo = ea;
    I32 ea_hi = ADD_IMM_I32(ea_lo, 4);

    if (i.def.rm & 1) {
      STORE_I32(ea_lo, LOAD_XFR_I32(i.def.rm & 0xe));
      STORE_I32(ea_hi, LOAD_XFR_I32(i.def.rm));
    } else {
      STORE_I32(ea_lo, LOAD_FPR_I32(i.def.rm));
      STORE_I32(ea_hi, LOAD_FPR_I32(i.def.rm | 0x1));
    }
  } else {
    I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
    STORE_GPR_I32(i.def.rn, ea);
    STORE_I32(ea, LOAD_FPR_I32(i.def.rm));
  }

  NEXT_INSTR();
}

/* FMOV.S  @Rm+,FRn 1111nnnnmmmm1001
   FMOV    @Rm+,DRn 1111nnn0mmmm1001
   FMOV    @Rm+,XDn 1111nnn1mmmm1001 */
INSTR(FMOV_RESTORE) {
  I32 ea = LOAD_GPR_I32(i.def.rm);

  if (FPU_DOUBLE_SZ) {
    I32 v_lo = LOAD_I32(ea);
    I32 v_hi = LOAD_I32(ADD_IMM_I32(ea, 4));
    if (i.def.rn & 1) {
      STORE_XFR_I32(i.def.rn & 0xe, v_lo);
      STORE_XFR_I32(i.def.rn, v_hi);
    } else {
      STORE_FPR_I32(i.def.rn, v_lo);
      STORE_FPR_I32(i.def.rn | 0x1, v_hi);
    }
    STORE_GPR_I32(i.def.rm, ADD_IMM_I32(ea, 8));
  } else {
    I32 v = LOAD_I32(ea);
    STORE_FPR_I32(i.def.rn, v);
    STORE_GPR_I32(i.def.rm, ADD_IMM_I32(ea, 4));
  }

  NEXT_INSTR();
}

/* FLDS FRm,FPUL 1111mmmm00011101 */
INSTR(FLDS) {
  I32 v = LOAD_FPR_I32(i.def.rn);
  STORE_FPUL_I32(v);
  NEXT_INSTR();
}

/* FSTS FPUL,FRn 1111nnnn00001101 */
INSTR(FSTS) {
  I32 v = LOAD_FPUL_I32();
  STORE_FPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* FABS FRn PR=0 1111nnnn01011101
   FABS DRn PR=1 1111nnn001011101 */
INSTR(FABS) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    F64 v = FABS_F64(LOAD_FPR_F64(n));
    STORE_FPR_F64(n, v);
  } else {
    int n = i.def.rn;
    F32 v = FABS_F32(LOAD_FPR_F32(n));
    STORE_FPR_F32(n, v);
  }
  NEXT_INSTR();
}

/* FSRRA FRn PR=0 1111nnnn01111101 */
INSTR(FSRRA) {
  F32 v = LOAD_FPR_F32(i.def.rn);
  v = FRSQRT_F32(v);
  STORE_FPR_F32(i.def.rn, v);
  NEXT_INSTR();
}

/* FADD FRm,FRn PR=0 1111nnnnmmmm0000
   FADD DRm,DRn PR=1 1111nnn0mmm00000 */
INSTR(FADD) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    int m = i.def.rm & 0xe;
    F64 drn = LOAD_FPR_F64(n);
    F64 drm = LOAD_FPR_F64(m);
    F64 v = FADD_F64(drn, drm);
    STORE_FPR_F64(n, v);
  } else {
    int n = i.def.rn;
    int m = i.def.rm;
    F32 frn = LOAD_FPR_F32(n);
    F32 frm = LOAD_FPR_F32(m);
    F32 v = FADD_F32(frn, frm);
    STORE_FPR_F32(n, v);
  }
  NEXT_INSTR();
}

/* FCMP/EQ FRm,FRn PR=0 1111nnnnmmmm0100
   FCMP/EQ DRm,DRn PR=1 1111nnn0mmm00100 */
INSTR(FCMPEQ) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    int m = i.def.rm & 0xe;
    F64 drn = LOAD_FPR_F64(n);
    F64 drm = LOAD_FPR_F64(m);
    I8 v = FCMPEQ_F64(drn, drm);
    STORE_T_I8(v);
  } else {
    int n = i.def.rn;
    int m = i.def.rm;
    F32 frn = LOAD_FPR_F32(n);
    F32 frm = LOAD_FPR_F32(m);
    I8 v = FCMPEQ_F32(frn, frm);
    STORE_T_I8(v);
  }
  NEXT_INSTR();
}

/* FCMP/GT FRm,FRn PR=0 1111nnnnmmmm0101
   FCMP/GT DRm,DRn PR=1 1111nnn0mmm00101 */
INSTR(FCMPGT) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    int m = i.def.rm & 0xe;
    F64 drn = LOAD_FPR_F64(n);
    F64 drm = LOAD_FPR_F64(m);
    I8 v = FCMPGT_F64(drn, drm);
    STORE_T_I8(v);
  } else {
    int n = i.def.rn;
    int m = i.def.rm;
    F32 frn = LOAD_FPR_F32(n);
    F32 frm = LOAD_FPR_F32(m);
    I8 v = FCMPGT_F32(frn, frm);
    STORE_T_I8(v);
  }
  NEXT_INSTR();
}

/* FDIV FRm,FRn PR=0 1111nnnnmmmm0011
   FDIV DRm,DRn PR=1 1111nnn0mmm00011 */
INSTR(FDIV) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    int m = i.def.rm & 0xe;
    F64 drn = LOAD_FPR_F64(n);
    F64 drm = LOAD_FPR_F64(m);
    F64 v = FDIV_F64(drn, drm);
    STORE_FPR_F64(n, v);
  } else {
    int n = i.def.rn;
    int m = i.def.rm;
    F32 frn = LOAD_FPR_F32(n);
    F32 frm = LOAD_FPR_F32(m);
    F32 v = FDIV_F32(frn, frm);
    STORE_FPR_F32(n, v);
  }
  NEXT_INSTR();
}

/* FLOAT FPUL,FRn PR=0 1111nnnn00101101
   FLOAT FPUL,DRn PR=1 1111nnn000101101 */
INSTR(FLOAT) {
  I32 fpul = LOAD_FPUL_I32();

  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    F64 v = ITOF_F64(SEXT_I32_I64(fpul));
    STORE_FPR_F64(n, v);
  } else {
    int n = i.def.rn;
    F32 v = ITOF_F32(fpul);
    STORE_FPR_F32(n, v);
  }
  NEXT_INSTR();
}

/* FMAC FR0,FRm,FRn PR=0 1111nnnnmmmm1110 */
INSTR(FMAC) {
  CHECK(!FPU_DOUBLE_PR);

  F32 frn = LOAD_FPR_F32(i.def.rn);
  F32 frm = LOAD_FPR_F32(i.def.rm);
  F32 fr0 = LOAD_FPR_F32(0);
  F32 v = FADD_F32(FMUL_F32(fr0, frm), frn);
  STORE_FPR_F32(i.def.rn, v);
  NEXT_INSTR();
}

/* FMUL FRm,FRn PR=0 1111nnnnmmmm0010
   FMUL DRm,DRn PR=1 1111nnn0mmm00010 */
INSTR(FMUL) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    int m = i.def.rm & 0xe;
    F64 drn = LOAD_FPR_F64(n);
    F64 drm = LOAD_FPR_F64(m);
    F64 v = FMUL_F64(drn, drm);
    STORE_FPR_F64(n, v);
  } else {
    int n = i.def.rn;
    int m = i.def.rm;
    F32 frn = LOAD_FPR_F32(n);
    F32 frm = LOAD_FPR_F32(m);
    F32 v = FMUL_F32(frn, frm);
    STORE_FPR_F32(n, v);
  }
  NEXT_INSTR();
}

/* FNEG FRn PR=0 1111nnnn01001101
   FNEG DRn PR=1 1111nnn001001101 */
INSTR(FNEG) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    F64 drn = LOAD_FPR_F64(n);
    F64 v = FNEG_F64(drn);
    STORE_FPR_F64(n, v);
  } else {
    int n = i.def.rn;
    F32 frn = LOAD_FPR_F32(n);
    F32 v = FNEG_F32(frn);
    STORE_FPR_F32(n, v);
  }
  NEXT_INSTR();
}

/* FSQRT FRn PR=0 1111nnnn01101101
   FSQRT DRn PR=1 1111nnnn01101101 */
INSTR(FSQRT) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    F64 drn = LOAD_FPR_F64(n);
    F64 v = FSQRT_F64(drn);
    STORE_FPR_F64(n, v);
  } else {
    int n = i.def.rn;
    F32 frn = LOAD_FPR_F32(n);
    F32 v = FSQRT_F32(frn);
    STORE_FPR_F32(n, v);
  }
  NEXT_INSTR();
}

/* FSUB FRm,FRn PR=0 1111nnnnmmmm0001
   FSUB DRm,DRn PR=1 1111nnn0mmm00001 */
INSTR(FSUB) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    int m = i.def.rm & 0xe;
    F64 drn = LOAD_FPR_F64(n);
    F64 drm = LOAD_FPR_F64(m);
    F64 v = FSUB_F64(drn, drm);
    STORE_FPR_F64(n, v);
  } else {
    int n = i.def.rn;
    int m = i.def.rm;
    F32 frn = LOAD_FPR_F32(n);
    F32 frm = LOAD_FPR_F32(m);
    F32 v = FSUB_F32(frn, frm);
    STORE_FPR_F32(n, v);
  }
  NEXT_INSTR();
}

/* FTRC FRm,FPUL PR=0 1111mmmm00111101
   FTRC DRm,FPUL PR=1 1111mmm000111101 */
INSTR(FTRC) {
  if (FPU_DOUBLE_PR) {
    int n = i.def.rn & 0xe;
    F64 drn = LOAD_FPR_F64(n);
    I32 dpv = FTOI_F32_I32(drn);
    STORE_FPUL_I32(dpv);
  } else {
    int n = i.def.rn;
    F32 frn = LOAD_FPR_F32(n);
    I32 spv = FTOI_F64_I32(frn);
    STORE_FPUL_I32(spv);
  }
  NEXT_INSTR();
}

/* FCNVDS DRm,FPUL PR=1 1111mmm010111101 */
INSTR(FCNVDS) {
  CHECK(FPU_DOUBLE_PR);

  /* TODO rounding modes? */

  int n = i.def.rn & 0xe;
  F64 dpv = LOAD_FPR_F64(n);
  F32 spv = FTRUNC_F64_F32(dpv);
  STORE_FPUL_F32(spv);
  NEXT_INSTR();
}

/* FCNVSD FPUL, DRn PR=1 1111nnn010101101 */
INSTR(FCNVSD) {
  CHECK(FPU_DOUBLE_PR);

  /* TODO rounding modes? */

  int n = i.def.rn & 0xe;
  F32 spv = LOAD_FPUL_F32();
  F64 dpv = FEXT_F32_F64(spv);
  STORE_FPR_F64(n, dpv);
  NEXT_INSTR();
}

/* LDS     Rm,FPSCR */
INSTR(LDSFPSCR) {
  I32 v = LOAD_GPR_I32(i.def.rn);
  STORE_FPSCR_I32(v);
  NEXT_INSTR();
}

/* LDS     Rm,FPUL */
INSTR(LDSFPUL) {
  I32 rn = LOAD_GPR_I32(i.def.rn);
  STORE_FPUL_I32(rn);
  NEXT_INSTR();
}

/* LDS.L   @Rm+,FPSCR */
INSTR(LDSMFPSCR) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_FPSCR_I32(v);
  ea = ADD_IMM_I32(ea, 4);
  STORE_GPR_I32(i.def.rn, ea);
  NEXT_INSTR();
}

/* LDS.L   @Rm+,FPUL */
INSTR(LDSMFPUL) {
  I32 ea = LOAD_GPR_I32(i.def.rn);
  I32 v = LOAD_I32(ea);
  STORE_FPUL_I32(v);
  ea = ADD_IMM_I32(ea, 4);
  STORE_GPR_I32(i.def.rn, ea);
  NEXT_INSTR();
}

/* STS     FPSCR,Rn */
INSTR(STSFPSCR) {
  I32 v = LOAD_FPSCR_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STS     FPUL,Rn */
INSTR(STSFPUL) {
  I32 v = LOAD_FPUL_I32();
  STORE_GPR_I32(i.def.rn, v);
  NEXT_INSTR();
}

/* STS.L   FPSCR,@-Rn */
INSTR(STSMFPSCR) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_FPSCR_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* STS.L   FPUL,@-Rn */
INSTR(STSMFPUL) {
  I32 ea = SUB_IMM_I32(LOAD_GPR_I32(i.def.rn), 4);
  STORE_GPR_I32(i.def.rn, ea);
  I32 v = LOAD_FPUL_I32();
  STORE_I32(ea, v);
  NEXT_INSTR();
}

/* FIPR FVm,FVn PR=0 1111nnmm11101101 */
INSTR(FIPR) {
  int m = (i.def.rn & 0x3) << 2;
  int n = (i.def.rn & 0xc);

  V128 fvn = LOAD_FPR_V128(n);
  V128 fvm = LOAD_FPR_V128(m);
  F32 dp = VDOT_F32(fvn, fvm);
  STORE_FPR_F32(n + 3, dp);
  NEXT_INSTR();
}

/* FSCA FPUL,DRn PR=0 1111nnn011111101 */
INSTR(FSCA) {
  I64 fsca_offset = ZEXT_I16_I64(LOAD_FPUL_I16());
  fsca_offset = SHL_IMM_I64(fsca_offset, 3);

  I64 ea = ADD_IMM_I64(fsca_offset, (int64_t)sh4_fsca_table);
  STORE_FPR_F32(i.def.rn, LOAD_HOST_F32(ea));
  STORE_FPR_F32(i.def.rn + 1, LOAD_HOST_F32(ADD_IMM_I64(ea, 4)));
  NEXT_INSTR();
}

/* FTRV XMTRX,FVn PR=0 1111nn0111111101 */
INSTR(FTRV) {
  int n = i.def.rn & 0xc;

  F32 el0 = LOAD_FPR_F32(n + 0);
  V128 col0 = LOAD_XFR_V128(0);
  V128 row0 = VBROADCAST_F32(el0);
  V128 result0 = VMUL_F32(col0, row0);

  F32 el1 = LOAD_FPR_F32(n + 1);
  V128 col1 = LOAD_XFR_V128(4);
  V128 row1 = VBROADCAST_F32(el1);
  V128 prod1 = VMUL_F32(col1, row1);
  V128 result1 = VADD_F32(result0, prod1);

  F32 el2 = LOAD_FPR_F32(n + 2);
  V128 col2 = LOAD_XFR_V128(8);
  V128 row2 = VBROADCAST_F32(el2);
  V128 prod2 = VMUL_F32(col2, row2);
  V128 result2 = VADD_F32(result1, prod2);

  F32 el3 = LOAD_FPR_F32(n + 3);
  V128 col3 = LOAD_XFR_V128(12);
  V128 row3 = VBROADCAST_F32(el3);
  V128 prod3 = VMUL_F32(col3, row3);
  V128 result3 = VADD_F32(result2, prod3);

  STORE_FPR_V128(n, result3);
  NEXT_INSTR();
}

/* FRCHG 1111101111111101 */
INSTR(FRCHG) {
  I32 fpscr = LOAD_FPSCR_I32();
  fpscr = XOR_IMM_I32(fpscr, FR_MASK);
  STORE_FPSCR_I32(fpscr);
  NEXT_INSTR();
}

/* FSCHG 1111001111111101 */
INSTR(FSCHG) {
  I32 fpscr = LOAD_FPSCR_I32();
  fpscr = XOR_IMM_I32(fpscr, SZ_MASK);
  STORE_FPSCR_I32(fpscr);
  NEXT_INSTR();
}
