#include "jit/frontend/armv3/armv3_fallback.h"
#include "core/core.h"
#include "jit/frontend/armv3/armv3_context.h"
#include "jit/frontend/armv3/armv3_disasm.h"
#include "jit/frontend/armv3/armv3_guest.h"

/* helper functions / macros for writing fallbacks */
#define FALLBACK(op)                                                 \
  void armv3_fallback_##op(struct armv3_guest *guest, uint32_t addr, \
                           union armv3_instr i)

#define CTX ((struct armv3_context *)guest->ctx)
#define MODE() (CTX->r[CPSR] & M_MASK)
#define REG(n) (CTX->r[n])
#define REG_USR(n) (*CTX->rusr[n])

#define CHECK_COND()                                  \
  if (!armv3_fallback_cond_check(CTX, i.raw >> 28)) { \
    REG(15) = addr + 4;                               \
    return;                                           \
  }

#define LOAD_RN(rn) armv3_fallback_load_rn(CTX, addr, rn)
#define LOAD_RD(rd) armv3_fallback_load_rd(CTX, addr, rd)

static inline int armv3_fallback_cond_check(struct armv3_context *ctx,
                                            uint32_t cond) {
  switch (cond) {
    case COND_EQ:
      return Z_SET(ctx->r[CPSR]);
    case COND_NE:
      return Z_CLEAR(ctx->r[CPSR]);
    case COND_CS:
      return C_SET(ctx->r[CPSR]);
    case COND_CC:
      return C_CLEAR(ctx->r[CPSR]);
    case COND_MI:
      return N_SET(ctx->r[CPSR]);
    case COND_PL:
      return N_CLEAR(ctx->r[CPSR]);
    case COND_VS:
      return V_SET(ctx->r[CPSR]);
    case COND_VC:
      return V_CLEAR(ctx->r[CPSR]);
    case COND_HI:
      return C_SET(ctx->r[CPSR]) && Z_CLEAR(ctx->r[CPSR]);
    case COND_LS:
      return C_CLEAR(ctx->r[CPSR]) || Z_SET(ctx->r[CPSR]);
    case COND_GE:
      return N_SET(ctx->r[CPSR]) == V_SET(ctx->r[CPSR]);
    case COND_LT:
      return N_SET(ctx->r[CPSR]) != V_SET(ctx->r[CPSR]);
    case COND_GT:
      return Z_CLEAR(ctx->r[CPSR]) &&
             N_SET(ctx->r[CPSR]) == V_SET(ctx->r[CPSR]);
    case COND_LE:
      return Z_SET(ctx->r[CPSR]) || N_SET(ctx->r[CPSR]) != V_SET(ctx->r[CPSR]);
    case COND_AL:
      return 1;
    case COND_NV:
    default:
      return 0;
  }
}

static inline void armv3_fallback_shift_lsl(uint32_t in, uint32_t n,
                                            uint32_t *out, uint32_t *carry) {
  /*
   * LSL by 32 has result zero carry out equal to bit 0 of input
   * LSL by more than 32 has result zero, carry out zero
   */
  uint64_t tmp = (uint64_t)in << n;
  *out = (uint32_t)tmp;
  *carry = (uint32_t)((tmp & 0x100000000) >> 32);
}

static inline void armv3_fallback_shift_lsr(uint32_t in, uint32_t n,
                                            uint32_t *out, uint32_t *carry) {
  /*
   * LSR by 32 has result zero, carry out equal to bit 31 of Rm
   * LSR by more than 32 has result zero, carry out zero
   */
  uint64_t tmp = (uint64_t)in;
  *out = (uint32_t)(tmp >> n);
  *carry = (uint32_t)((tmp >> (n - 1)) & 0x1);
}

static inline void armv3_fallback_shift_asr(uint32_t in, uint32_t n,
                                            uint32_t *out, uint32_t *carry) {
  /*
   * ASR by 32 or more has result filled with and carry out equal to bit 31 of
   * input
   */
  int64_t tmp = (int64_t)(int32_t)in;
  *out = (uint32_t)(tmp >> n);
  *carry = (uint32_t)((tmp >> (n - 1)) & 0x1);
}

static inline void armv3_fallback_shift_ror(uint32_t in, uint32_t n,
                                            uint32_t *out, uint32_t *carry) {
  /*
   * ROR by 32 has result equal to input, carry out equal to bit 31 of input
   * ROR by n where n is greater than 32 will give the same result and carry
   * out as ROR by n-32; therefore repeatedly subtract 32 from n until the
   * amount is in the range 1 to 32 and see above
   */
  uint64_t tmp = (uint64_t)in;
  n &= 31;
  *out = (uint32_t)((tmp << (32 - n)) | (tmp >> n));
  *carry = (*out >> 31) & 0x1;
}

static void armv3_fallback_shift(const struct armv3_context *ctx,
                                 enum armv3_shift_source src,
                                 enum armv3_shift_type type, uint32_t in,
                                 uint32_t n, uint32_t *out, uint32_t *carry) {
  *out = in;
  *carry = C_SET(ctx->r[CPSR]);

  if (src == SHIFT_REG) {
    n = ctx->r[n];
  }

  if (n) {
    switch (type) {
      case SHIFT_LSL:
        armv3_fallback_shift_lsl(in, n, out, carry);
        break;
      case SHIFT_LSR:
        armv3_fallback_shift_lsr(in, n, out, carry);
        break;
      case SHIFT_ASR:
        armv3_fallback_shift_asr(in, n, out, carry);
        break;
      case SHIFT_ROR:
        armv3_fallback_shift_ror(in, n, out, carry);
        break;
      default:
        LOG_FATAL("Unsupported shift type");
        break;
    }
  }
}

static inline void armv3_fallback_parse_shift(struct armv3_context *ctx,
                                              uint32_t addr, uint32_t reg,
                                              uint32_t shift, uint32_t *value,
                                              uint32_t *carry) {
  enum armv3_shift_source src;
  enum armv3_shift_type type;
  uint32_t n;
  armv3_disasm_shift(shift, &src, &type, &n);

  uint32_t data;
  if (reg == 15) {
    /*
     * if the shift amount is specified in the instruction, PC will be 8 bytes
     * ahead. if a register is used to specify the shift amount the PC will be
     * 12 bytes ahead
     */
    if (src == SHIFT_IMM) {
      data = addr + 8;
    } else {
      data = addr + 12;
    }
  } else {
    data = ctx->r[reg];
  }
  armv3_fallback_shift(ctx, src, type, data, n, value, carry);
}

static inline uint32_t armv3_fallback_load_rn(struct armv3_context *ctx,
                                              uint32_t addr, int rn) {
  if (rn == 15) {
    /* account for instruction prefetching if loading the pc */
    return addr + 8;
  }

  return ctx->r[rn];
}

static inline uint32_t armv3_fallback_load_rd(struct armv3_context *ctx,
                                              uint32_t addr, int rd) {
  if (rd == 15) {
    /* account for instruction prefetching if loading the pc */
    return addr + 12;
  }

  return ctx->r[rd];
}

/*
 * branch and branch with link
 */
#define BRANCH_OFFSET() armv3_disasm_offset(i.branch.offset)

FALLBACK(B) {
  CHECK_COND();

  REG(15) = addr + 8 + BRANCH_OFFSET();
}

FALLBACK(BL) {
  CHECK_COND();

  REG(14) = addr + 4;
  REG(15) = addr + 8 + BRANCH_OFFSET();
}

/*
 * data processing
 */
#define PARSE_OP2(value, carry) \
  armv3_fallback_parse_op2(CTX, addr, i, value, carry)

#define CARRY() (C_SET(CTX->r[CPSR]))

#define UPDATE_FLAGS_LOGICAL()                            \
  if (i.data.s) {                                         \
    armv3_fallback_update_flags_logical(CTX, res, carry); \
    if (i.data.rd == 15) {                                \
      guest->restore_mode(guest->data);                   \
    }                                                     \
  }

#define UPDATE_FLAGS_SUB()                               \
  if (i.data.s) {                                        \
    armv3_fallback_update_flags_sub(CTX, lhs, rhs, res); \
    if (i.data.rd == 15) {                               \
      guest->restore_mode(guest->data);                  \
    }                                                    \
  }

#define UPDATE_FLAGS_ADD()                               \
  if (i.data.s) {                                        \
    armv3_fallback_update_flags_add(CTX, lhs, rhs, res); \
    if (i.data.rd == 15) {                               \
      guest->restore_mode(guest->data);                  \
    }                                                    \
  }

#define MAKE_CPSR(cpsr, n, z, c, v)                                   \
  (((cpsr) & ~(N_MASK | Z_MASK | C_MASK | V_MASK)) | ((n) << N_BIT) | \
   ((z) << Z_BIT) | ((c) << C_BIT) | ((v) << V_BIT))

static inline void armv3_fallback_parse_op2(struct armv3_context *ctx,
                                            uint32_t addr, union armv3_instr i,
                                            uint32_t *value, uint32_t *carry) {
  if (i.data.i) {
    /* op2 is an immediate */
    uint32_t n = i.data_imm.rot << 1;

    if (n) {
      armv3_fallback_shift_ror(i.data_imm.imm, n, value, carry);
    } else {
      *value = i.data_imm.imm;
      *carry = C_SET(ctx->r[CPSR]);
    }
  } else {
    /* op2 is as shifted register */
    armv3_fallback_parse_shift(ctx, addr, i.data_reg.rm, i.data_reg.shift,
                               value, carry);
  }
}

static inline void armv3_fallback_update_flags_logical(
    struct armv3_context *ctx, uint32_t res, uint32_t carry) {
  int n = (res & 0x80000000) ? 1 : 0;
  int z = res ? 0 : 1;
  int c = carry;
  int v = 0;
  ctx->r[CPSR] = MAKE_CPSR(ctx->r[CPSR], n, z, c, v);
}

static inline void armv3_fallback_update_flags_sub(struct armv3_context *ctx,
                                                   uint32_t lhs, uint32_t rhs,
                                                   uint32_t res) {
  int n = res & 0x80000000 ? 1 : 0;
  int z = res ? 0 : 1;
  int c = ~((~lhs & rhs) | ((~lhs | rhs) & res)) >> 31;
  int v = ((lhs ^ rhs) & (res ^ lhs)) >> 31;
  ctx->r[CPSR] = MAKE_CPSR(ctx->r[CPSR], n, z, c, v);
}

static inline void armv3_fallback_update_flags_add(struct armv3_context *ctx,
                                                   uint32_t lhs, uint32_t rhs,
                                                   uint32_t res) {
  int n = res & 0x80000000 ? 1 : 0;
  int z = res ? 0 : 1;
  int c = ((lhs & rhs) | ((lhs | rhs) & ~res)) >> 31;
  int v = ((res ^ lhs) & (res ^ rhs)) >> 31;
  ctx->r[CPSR] = MAKE_CPSR(ctx->r[CPSR], n, z, c, v);
}

FALLBACK(INVALID) {}

FALLBACK(AND) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs & rhs;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_LOGICAL();
}

FALLBACK(EOR) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs ^ rhs;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_LOGICAL();
}

FALLBACK(SUB) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs - rhs;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_SUB();
}

FALLBACK(RSB) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  PARSE_OP2(&lhs, &carry);
  rhs = LOAD_RN(i.data.rn);
  res = lhs - rhs;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_SUB();
}

FALLBACK(ADD) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs + rhs;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_ADD();
}

FALLBACK(ADC) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs + rhs + CARRY();

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_ADD();
}

FALLBACK(SBC) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs - rhs + CARRY() - 1;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_SUB();
}

FALLBACK(RSC) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  PARSE_OP2(&lhs, &carry);
  rhs = LOAD_RN(i.data.rn);
  res = lhs - rhs + CARRY() - 1;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_SUB();
}

FALLBACK(TST) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs & rhs;

  REG(15) = addr + 4;
  UPDATE_FLAGS_LOGICAL();
}

FALLBACK(TEQ) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs ^ rhs;

  REG(15) = addr + 4;
  UPDATE_FLAGS_LOGICAL();
}

FALLBACK(CMP) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs - rhs;

  REG(15) = addr + 4;
  UPDATE_FLAGS_SUB();
}

FALLBACK(CMN) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs + rhs;

  REG(15) = addr + 4;
  UPDATE_FLAGS_ADD();
}

FALLBACK(ORR) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs | rhs;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_LOGICAL();
}

FALLBACK(MOV) {
  CHECK_COND();

  uint32_t res, carry;
  PARSE_OP2(&res, &carry);

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_LOGICAL();
}

FALLBACK(BIC) {
  CHECK_COND();

  uint32_t lhs, rhs, res, carry;
  lhs = LOAD_RN(i.data.rn);
  PARSE_OP2(&rhs, &carry);
  res = lhs & ~rhs;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_LOGICAL();
}

FALLBACK(MVN) {
  CHECK_COND();

  uint32_t rhs, res, carry;
  PARSE_OP2(&rhs, &carry);
  res = ~rhs;

  REG(15) = addr + 4;
  REG(i.data.rd) = res;
  UPDATE_FLAGS_LOGICAL();
}

/*
 * psr transfer
 */
FALLBACK(MRS) {
  CHECK_COND();

  if (i.mrs.src_psr) {
    REG(i.mrs.rd) = REG(SPSR);
  } else {
    REG(i.mrs.rd) = REG(CPSR);
  }

  REG(15) = addr + 4;
}

FALLBACK(MSR) {
  CHECK_COND();

  uint32_t newsr;
  if (i.msr.i) {
    uint32_t carry;
    armv3_fallback_shift_ror(i.msr_imm.imm, i.msr_imm.rot << 1, &newsr, &carry);
  } else {
    newsr = REG(i.msr_reg.rm);
  }

  if (i.msr.dst_psr) {
    uint32_t oldsr = REG(SPSR);

    /* control flags can't be modified when all bit isn't set */
    if (!i.msr.all) {
      newsr = (newsr & 0xf0000000) | (oldsr & 0x0fffffff);
    }

    /* SPSR can't be modified in user and system mode */
    if (MODE() > MODE_USR && MODE() < MODE_SYS) {
      REG(SPSR) = newsr;
    }
  } else {
    uint32_t oldsr = REG(CPSR);

    /* control flags can't be modified when all bit isn't set / in user mode */
    if (!i.msr.all || MODE() == MODE_USR) {
      newsr = (newsr & 0xf0000000) | (oldsr & 0x0fffffff);
    }

    guest->switch_mode(guest->data, newsr);
  }

  REG(15) = addr + 4;
}

/*
 * multiply and multiply-accumulate
 */
#define UPDATE_FLAGS_MUL()                     \
  if (i.mul.s) {                               \
    armv3_fallback_update_flags_mul(CTX, res); \
  }

#define MAKE_CPSR_NZ(cpsr, n, z) \
  (((cpsr) & ~(N_MASK | Z_MASK)) | ((n) << N_BIT) | ((z) << Z_BIT))

static void armv3_fallback_update_flags_mul(struct armv3_context *ctx,
                                            uint32_t res) {
  int n = res & 0x80000000 ? 1 : 0;
  int z = res ? 0 : 1;
  ctx->r[CPSR] = MAKE_CPSR_NZ(ctx->r[CPSR], n, z);
}

FALLBACK(MUL) {
  CHECK_COND();

  uint32_t a = REG(i.mul.rm);
  uint32_t b = REG(i.mul.rs);
  uint32_t res = a * b;

  REG(15) = addr + 4;
  REG(i.mul.rd) = res;
  UPDATE_FLAGS_MUL();
}

FALLBACK(MLA) {
  CHECK_COND();

  uint32_t a = REG(i.mul.rm);
  uint32_t b = REG(i.mul.rs);
  uint32_t c = REG(i.mul.rn);
  uint32_t res = a * b + c;

  REG(15) = addr + 4;
  REG(i.mul.rd) = res;
  UPDATE_FLAGS_MUL();
}

/*
 * single data transfer
 */
static inline void armv3_fallback_memop(struct armv3_guest *guest,
                                        uint32_t addr, union armv3_instr i) {
  CHECK_COND();

  /* parse offset */
  uint32_t offset = 0;
  if (i.xfr.i) {
    uint32_t carry;
    armv3_fallback_parse_shift(CTX, addr, i.xfr_reg.rm, i.xfr_reg.shift,
                               &offset, &carry);
  } else {
    offset = i.xfr_imm.imm;
  }

  uint32_t base = LOAD_RN(i.xfr.rn);
  uint32_t final = i.xfr.u ? base + offset : base - offset;
  uint32_t ea = i.xfr.p ? final : base;

  /*
   * writeback is applied in pipeline before memory is read.
   * note, post-increment mode always writes back
   */
  if (i.xfr.w || !i.xfr.p) {
    REG(i.xfr.rn) = final;
  }

  if (i.xfr.l) {
    /* load data */
    uint32_t data = 0;
    if (i.xfr.b) {
      data = guest->r8(guest->mem, ea);
    } else {
      data = guest->r32(guest->mem, ea);
    }

    REG(15) = addr + 4;
    REG(i.xfr.rd) = data;
  } else {
    /* store data */
    uint32_t data = LOAD_RD(i.xfr.rd);
    if (i.xfr.b) {
      guest->w8(guest->mem, ea, data);
    } else {
      guest->w32(guest->mem, ea, data);
    }

    REG(15) = addr + 4;
  }
}

FALLBACK(LDR) {
  armv3_fallback_memop(guest, addr, i);
}

FALLBACK(STR) {
  armv3_fallback_memop(guest, addr, i);
}

/*
 * block data transfer
 */
FALLBACK(LDM) {
  CHECK_COND();

  uint32_t base = LOAD_RN(i.blk.rn);
  uint32_t offset = popcnt32(i.blk.rlist) * 4;
  uint32_t final = i.blk.u ? base + offset : base - offset;
  uint32_t ea = base;

  /* writeback is applied in pipeline before memory is read */
  if (i.blk.w) {
    REG(i.blk.rn) = final;
  }

  REG(15) = addr + 4;

  for (int bit = 0; bit < 16; bit++) {
    int reg = bit;

    if (!i.blk.u) {
      reg = 15 - bit;
    }

    if (i.blk.rlist & (1 << reg)) {
      /* pre-increment */
      if (i.blk.p) {
        ea = i.blk.u ? ea + 4 : ea - 4;
      }

      /* user bank transfer */
      if (i.blk.s && (i.blk.rlist & 0x8000) == 0) {
        reg = REG_USR(reg);
      }

      REG(reg) = guest->r32(guest->mem, ea);

      /* post-increment */
      if (!i.blk.p) {
        ea = i.blk.u ? ea + 4 : ea - 4;
      }
    }
  }

  if ((i.blk.rlist & 0x8000) && i.blk.s) {
    /* move SPSR into CPSR */
    guest->restore_mode(guest->data);
  }
}

FALLBACK(STM) {
  CHECK_COND();

  uint32_t base = LOAD_RN(i.blk.rn);
  uint32_t offset = popcnt32(i.blk.rlist) * 4;
  uint32_t final = i.blk.u ? base + offset : base - offset;
  int wrote = 0;

  for (int bit = 0; bit < 16; bit++) {
    int reg = bit;

    if (!i.blk.u) {
      reg = 15 - bit;
    }

    if (i.blk.rlist & (1 << reg)) {
      /* pre-increment */
      if (i.blk.p) {
        base = i.blk.u ? base + 4 : base - 4;
      }

      /* user bank transfer */
      if (i.blk.s) {
        reg = REG_USR(reg);
      }

      uint32_t data = LOAD_RD(reg);
      guest->w32(guest->mem, base, data);

      /* post-increment */
      if (!i.blk.p) {
        base = i.blk.u ? base + 4 : base - 4;
      }

      /*
       * when write-back is specified, the base is written back at the end of
       * the second cycle of the instruction. during a STM, the first register
       * is written out at the start of the second cycle. a STM which includes
       * storing the base, with the base as the first register to be stored,
       * will therefore store the unchanged value, whereas with the base second
       * or later in the transfer order, will store the modified value
       */
      if (i.blk.w && !wrote) {
        REG(i.blk.rn) = final;
        wrote = 1;
      }
    }
  }

  REG(15) = addr + 4;
}

/*
 * single data swap
 */
FALLBACK(SWP) {
  CHECK_COND();

  uint32_t ea = REG(i.swp.rn);
  uint32_t new = REG(i.swp.rm);
  uint32_t old = 0;

  if (i.swp.b) {
    old = guest->r8(guest->mem, ea);
    guest->w8(guest->mem, ea, new);
  } else {
    old = guest->r32(guest->mem, ea);
    guest->w32(guest->mem, ea, new);
  }

  REG(15) = addr + 4;
  REG(i.swp.rd) = old;
}

/*
 * software interrupt
 */
FALLBACK(SWI) {
  CHECK_COND();

  uint32_t oldsr = REG(CPSR);
  uint32_t newsr = (oldsr & ~M_MASK) | I_MASK | MODE_SVC;

  guest->switch_mode(guest->data, newsr);
  REG(14) = addr + 4;
  REG(15) = 0x8;

  LOG_WARNING("SWI");
}
