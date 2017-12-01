#include "jit/backend/x64/x64_local.h"

extern "C" {
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "jit/jit_guest.h"
}

#define EMITTER(op, constraints)                                           \
  void x64_emit_##op(struct x64_backend *, Xbyak::CodeGenerator &,         \
                     struct ir *, struct ir_instr *);                      \
  static struct _x64_##op##_init {                                         \
    _x64_##op##_init() {                                                   \
      x64_emitters[OP_##op] = {(void *)&x64_emit_##op, constraints};       \
    }                                                                      \
  } x64_##op##_init;                                                       \
  void x64_emit_##op(struct x64_backend *backend, Xbyak::CodeGenerator &e, \
                     struct ir *ir, struct ir_instr *instr)

#define CONSTRAINTS(result_flags, ...) \
  result_flags, {                      \
    __VA_ARGS__                        \
  }

#define RES instr->result
#define ARG0 instr->arg[0]
#define ARG1 instr->arg[1]
#define ARG2 instr->arg[2]
#define ARG3 instr->arg[3]

#define RES_REG x64_backend_reg(backend, RES)
#define ARG0_REG x64_backend_reg(backend, ARG0)
#define ARG1_REG x64_backend_reg(backend, ARG1)
#define ARG2_REG x64_backend_reg(backend, ARG2)
#define ARG3_REG x64_backend_reg(backend, ARG3)

#define RES_XMM x64_backend_xmm(backend, RES)
#define ARG0_XMM x64_backend_xmm(backend, ARG0)
#define ARG1_XMM x64_backend_xmm(backend, ARG1)
#define ARG2_XMM x64_backend_xmm(backend, ARG2)
#define ARG3_XMM x64_backend_xmm(backend, ARG3)

enum {
  NONE = 0,
  REG_ARG0 = JIT_REG_I64 | JIT_REUSE_ARG0,
  REG_I64 = JIT_REG_I64,
  REG_F64 = JIT_REG_F64,
  REG_V128 = JIT_REG_V128,
  REG_ALL = REG_I64 | REG_F64 | REG_V128,
  IMM_I32 = JIT_IMM_I32,
  IMM_I64 = JIT_IMM_I64,
  IMM_F32 = JIT_IMM_F32,
  IMM_F64 = JIT_IMM_F64,
  IMM_BLK = JIT_IMM_BLK,
  IMM_ALL = IMM_I32 | IMM_I64 | IMM_F32 | IMM_F64 | IMM_BLK,
  VAL_I64 = REG_I64 | IMM_I64,
  VAL_ALL = REG_ALL | IMM_ALL,
  OPT = JIT_OPTIONAL,
  OPT_I64 = OPT | VAL_I64,
};

struct jit_emitter x64_emitters[IR_NUM_OPS];

EMITTER(SOURCE_INFO, CONSTRAINTS(NONE, IMM_I32, IMM_I32)) {
#if 0
  /* encode the guest address of each instruction in the generated code for
     debugging purposes */
  Xbyak::Label skip;
  e.jmp(skip);
  e.dd(ARG0->i32);
  e.L(skip);
#endif
}

EMITTER(FALLBACK, CONSTRAINTS(NONE, IMM_I64, IMM_I32, IMM_I32)) {
  struct jit_guest *guest = backend->base.guest;
  void *fallback = (void *)ARG0->i64;
  uint32_t addr = ARG1->i32;
  uint32_t raw_instr = ARG2->i32;

  e.mov(arg0, (uint64_t)guest);
  e.mov(arg1, addr);
  e.mov(arg2, raw_instr);
  e.call(fallback);
}

EMITTER(LOAD_HOST, CONSTRAINTS(REG_ALL, REG_I64)) {
  struct ir_value *dst = RES;
  Xbyak::Reg src = ARG0_REG;

  x64_backend_load_mem(backend, dst, src);
}

EMITTER(STORE_HOST, CONSTRAINTS(NONE, REG_I64, VAL_ALL)) {
  Xbyak::Reg dst = ARG0_REG;
  struct ir_value *data = ARG1;

  x64_backend_store_mem(backend, dst, data);
}

EMITTER(LOAD_GUEST, CONSTRAINTS(REG_ALL, REG_I64 | IMM_I32)) {
  struct jit_guest *guest = backend->base.guest;
  Xbyak::Reg dst = RES_REG;
  struct ir_value *addr = ARG0;

  if (ir_is_constant(addr)) {
    /* peel away one layer of abstraction and directly access the backing
       memory or directly invoke the callback when the address is constant */
    void *userdata;
    uint8_t *ptr;
    mem_read_cb read;
    guest->lookup(guest->mem, addr->i32, &userdata, &ptr, &read, NULL);

    if (ptr) {
      e.mov(e.rax, (uint64_t)ptr);
      x64_backend_load_mem(backend, RES, e.rax);
    } else {
      int data_size = ir_type_size(RES->type);
      uint32_t data_mask = (1 << (data_size * 8)) - 1;

      e.mov(arg0, (uint64_t)userdata);
      e.mov(arg1, (uint32_t)addr->i32);
      e.mov(arg2, data_mask);
      e.call((void *)read);
      e.mov(dst, e.rax);
    }
  } else {
    Xbyak::Reg ra = x64_backend_reg(backend, addr);

    void *fn = nullptr;
    switch (RES->type) {
      case VALUE_I8:
        fn = (void *)guest->r8;
        break;
      case VALUE_I16:
        fn = (void *)guest->r16;
        break;
      case VALUE_I32:
        fn = (void *)guest->r32;
        break;
      case VALUE_I64:
        fn = (void *)guest->r64;
        break;
      default:
        LOG_FATAL("unexpected load result type");
        break;
    }

    e.mov(arg0, (uint64_t)guest->mem);
    e.mov(arg1, ra);
    e.call((void *)fn);
    e.mov(dst, e.rax);
  }
}

EMITTER(STORE_GUEST, CONSTRAINTS(NONE, REG_I64 | IMM_I32, VAL_ALL)) {
  struct jit_guest *guest = backend->base.guest;
  struct ir_value *addr = ARG0;
  struct ir_value *data = ARG1;

  if (ir_is_constant(addr)) {
    /* peel away one layer of abstraction and directly access the backing
       memory or directly invoke the callback when the address is constant */
    void *userdata;
    uint8_t *ptr;
    mem_write_cb write;
    guest->lookup(guest->mem, addr->i32, &userdata, &ptr, NULL, &write);

    if (ptr) {
      e.mov(e.rax, (uint64_t)ptr);
      x64_backend_store_mem(backend, e.rax, data);
    } else {
      int data_size = ir_type_size(data->type);
      uint32_t data_mask = (1 << (data_size * 8)) - 1;

      e.mov(arg0, (uint64_t)userdata);
      e.mov(arg1, (uint32_t)addr->i32);
      x64_backend_mov_value(backend, arg2, data);
      e.mov(arg3, data_mask);
      e.call((void *)write);
    }
  } else {
    Xbyak::Reg ra = x64_backend_reg(backend, addr);

    void *fn = nullptr;
    switch (data->type) {
      case VALUE_I8:
        fn = (void *)guest->w8;
        break;
      case VALUE_I16:
        fn = (void *)guest->w16;
        break;
      case VALUE_I32:
        fn = (void *)guest->w32;
        break;
      case VALUE_I64:
        fn = (void *)guest->w64;
        break;
      default:
        LOG_FATAL("unexpected store value type");
        break;
    }

    e.mov(arg0, (uint64_t)guest->mem);
    e.mov(arg1, ra);
    x64_backend_mov_value(backend, arg2, data);
    e.call((void *)fn);
  }
}

EMITTER(LOAD_FAST, CONSTRAINTS(REG_ALL, REG_I64)) {
  struct ir_value *dst = RES;
  Xbyak::Reg addr = ARG0_REG;

  x64_backend_load_mem(backend, dst, addr.cvt64() + guestmem);
}

EMITTER(STORE_FAST, CONSTRAINTS(NONE, REG_I64, VAL_ALL)) {
  Xbyak::Reg addr = ARG0_REG;
  struct ir_value *data = ARG1;

  x64_backend_store_mem(backend, addr.cvt64() + guestmem, data);
}

EMITTER(LOAD_CONTEXT, CONSTRAINTS(REG_ALL, IMM_I32)) {
  struct ir_value *dst = RES;
  int offset = ARG0->i32;

  x64_backend_load_mem(backend, dst, guestctx + offset);
}

EMITTER(STORE_CONTEXT, CONSTRAINTS(NONE, IMM_I32, VAL_ALL)) {
  int offset = ARG0->i32;
  struct ir_value *data = ARG1;

  x64_backend_store_mem(backend, guestctx + offset, data);
}

EMITTER(LOAD_LOCAL, CONSTRAINTS(REG_ALL, IMM_I32)) {
  struct ir_value *dst = RES;
  int offset = X64_STACK_LOCALS + ARG0->i32;

  x64_backend_load_mem(backend, dst, e.rsp + offset);
}

EMITTER(STORE_LOCAL, CONSTRAINTS(NONE, IMM_I32, VAL_ALL)) {
  int offset = X64_STACK_LOCALS + ARG0->i32;
  struct ir_value *data = ARG1;

  x64_backend_store_mem(backend, e.rsp + offset, data);
}

EMITTER(FTOI, CONSTRAINTS(REG_I64, REG_F64)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Xmm ra = ARG0_XMM;

  switch (RES->type) {
    case VALUE_I32: {
      /* cvttss2si saturates both underflows and overflows to INT32_MIN, while
         OP_FTOI should saturate underflows to INT32_MIN and overflows to
         INT32_MAX. due to this difference, the value must be manually clamped
         beforehand */
      Xbyak::Address min_int32 =
          x64_backend_xmm_constant(backend, XMM_CONST_PD_MIN_INT32);
      Xbyak::Address max_int32 =
          x64_backend_xmm_constant(backend, XMM_CONST_PD_MAX_INT32);

      /* INT32_MIN and INT32_MAX can't be encoded as floats, but can be encoded
         as doubles. extend float to double so clamp occurs between doubles */
      if (ARG0->type == VALUE_F32) {
        e.cvtss2sd(e.xmm0, ra);
      } else {
        e.movsd(e.xmm0, ra);
      }
      /* clamp double to [INT32_MIN, INT32_MAX] */
      e.maxsd(e.xmm0, min_int32);
      e.minsd(e.xmm0, max_int32);
      /* now convert double to integer */
      e.cvttsd2si(rd, e.xmm0);
    } break;
    default:
      LOG_FATAL("unexpected result type");
      break;
  }
}

EMITTER(ITOF, CONSTRAINTS(REG_F64, REG_I64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Reg ra = ARG0_REG;

  switch (RES->type) {
    case VALUE_F32:
      CHECK_EQ(ARG0->type, VALUE_I32);
      e.cvtsi2ss(rd, ra);
      break;
    case VALUE_F64:
      CHECK_EQ(ARG0->type, VALUE_I64);
      e.cvtsi2sd(rd, ra);
      break;
    default:
      LOG_FATAL("unexpected result type");
      break;
  }
}

EMITTER(SEXT, CONSTRAINTS(REG_I64, REG_I64)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg ra = ARG0_REG;

  if (ra == rd) {
    /* already the correct width */
    return;
  }

  if (rd.isBit(64) && ra.isBit(32)) {
    e.movsxd(rd.cvt64(), ra);
  } else {
    e.movsx(rd, ra);
  }
}

EMITTER(ZEXT, CONSTRAINTS(REG_I64, REG_I64)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg ra = ARG0_REG;

  if (ra == rd) {
    /* already the correct width */
    return;
  }

  if (rd.isBit(64) && ra.isBit(32)) {
    /* mov will automatically zero fill the upper 32-bits */
    e.mov(rd.cvt32(), ra);
  } else {
    e.movzx(rd, ra);
  }
}

EMITTER(TRUNC, CONSTRAINTS(REG_I64, REG_I64)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg ra = ARG0_REG;

  if (ra.getIdx() == rd.getIdx()) {
    /* noop if already the same register. note, this means the high order bits
       of the result won't be cleared, but I believe that is fine */
    return;
  }

  switch (RES->type) {
    case VALUE_I8:
      ra = ra.cvt8();
      break;
    case VALUE_I16:
      ra = ra.cvt16();
      break;
    case VALUE_I32:
      ra = ra.cvt32();
      break;
    default:
      LOG_FATAL("unexpected value type");
  }

  if (ra.isBit(32)) {
    /* mov will automatically zero fill the upper 32-bits */
    e.mov(rd, ra);
  } else {
    e.movzx(rd.cvt32(), ra);
  }
}

EMITTER(FEXT, CONSTRAINTS(REG_F64, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  e.cvtss2sd(rd, ra);
}

EMITTER(FTRUNC, CONSTRAINTS(REG_F64, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  e.cvtsd2ss(rd, ra);
}

EMITTER(SELECT, CONSTRAINTS(REG_I64, REG_I64, REG_I64, REG_I64)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg t = ARG0_REG;
  Xbyak::Reg f = ARG1_REG;
  Xbyak::Reg cond = ARG2_REG;

  /* convert result to Reg32e to please xbyak */
  CHECK_GE(rd.getBit(), 32);
  Xbyak::Reg32e rd_32e(rd.getIdx(), rd.getBit());

  e.test(cond, cond);
  if (rd_32e != t) {
    e.cmovnz(rd_32e, t);
  }
  e.cmovz(rd_32e, f);
}

EMITTER(CMP, CONSTRAINTS(REG_I64, REG_I64, REG_I64 | IMM_I32, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg ra = ARG0_REG;

  if (ir_is_constant(ARG1)) {
    e.cmp(ra, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.cmp(ra, rb);
  }

  enum ir_cmp cmp = (enum ir_cmp)ARG2->i32;
  switch (cmp) {
    case CMP_EQ:
      e.sete(rd);
      break;
    case CMP_NE:
      e.setne(rd);
      break;
    case CMP_SGE:
      e.setge(rd);
      break;
    case CMP_SGT:
      e.setg(rd);
      break;
    case CMP_UGE:
      e.setae(rd);
      break;
    case CMP_UGT:
      e.seta(rd);
      break;
    case CMP_SLE:
      e.setle(rd);
      break;
    case CMP_SLT:
      e.setl(rd);
      break;
    case CMP_ULE:
      e.setbe(rd);
      break;
    case CMP_ULT:
      e.setb(rd);
      break;
    default:
      LOG_FATAL("unexpected comparison type");
  }
}

EMITTER(FCMP, CONSTRAINTS(REG_I64, REG_F64, REG_F64, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Xmm ra = ARG0_XMM;
  Xbyak::Xmm rb = ARG1_XMM;

  if (ARG0->type == VALUE_F32) {
    e.ucomiss(ra, rb);
  } else {
    e.ucomisd(ra, rb);
  }

  enum ir_cmp cmp = (enum ir_cmp)ARG2->i32;
  switch (cmp) {
    case CMP_EQ:
      e.mov(e.eax, 0);
      /* if NaN set rd to 0, else set rd to 1 */
      e.setnp(rd);
      /* if NaN or equal nop, else set rd to 0 */
      e.cmovne(rd, e.eax);
      break;
    case CMP_NE:
      e.mov(e.eax, 1);
      /* if NaN set rd to 1, else set rd to 0 */
      e.setp(rd);
      /* if NaN or equal nop, else set rd to 1 */
      e.cmovne(rd, e.eax);
      break;
    case CMP_SGE:
      e.setae(rd);
      break;
    case CMP_SGT:
      e.seta(rd);
      break;
    case CMP_SLE:
      e.setbe(rd);
      break;
    case CMP_SLT:
      e.setb(rd);
      break;
    default:
      LOG_FATAL("unexpected comparison type");
  }
}

EMITTER(ADD, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64 | IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.add(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.add(rd, rb);
  }
}

EMITTER(SUB, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64 | IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.sub(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.sub(rd, rb);
  }
}

EMITTER(SMUL, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg rb = ARG1_REG;

  e.imul(rd, rb);
}

EMITTER(UMUL, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg rb = ARG1_REG;

  e.imul(rd, rb);
}

EMITTER(DIV, CONSTRAINTS(NONE)) {
  LOG_FATAL("unsupported");
}

EMITTER(NEG, CONSTRAINTS(REG_ARG0, REG_I64)) {
  Xbyak::Reg rd = RES_REG;

  e.neg(rd);
}

EMITTER(ABS, CONSTRAINTS(NONE)) {
  LOG_FATAL("unsupported");
}

EMITTER(FADD, CONSTRAINTS(REG_F64, REG_F64, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;
  Xbyak::Xmm rb = ARG1_XMM;

  if (RES->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vaddss(rd, ra, rb);
    } else {
      if (rd != ra) {
        e.movss(rd, ra);
      }
      e.addss(rd, rb);
    }
  } else {
    if (X64_USE_AVX) {
      e.vaddsd(rd, ra, rb);
    } else {
      if (rd != ra) {
        e.movsd(rd, ra);
      }
      e.addsd(rd, rb);
    }
  }
}

EMITTER(FSUB, CONSTRAINTS(REG_F64, REG_F64, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;
  Xbyak::Xmm rb = ARG1_XMM;

  if (RES->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vsubss(rd, ra, rb);
    } else {
      if (rd != ra) {
        e.movss(rd, ra);
      }
      e.subss(rd, rb);
    }
  } else {
    if (X64_USE_AVX) {
      e.vsubsd(rd, ra, rb);
    } else {
      if (rd != ra) {
        e.movsd(rd, ra);
      }
      e.subsd(rd, rb);
    }
  }
}

EMITTER(FMUL, CONSTRAINTS(REG_F64, REG_F64, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;
  Xbyak::Xmm rb = ARG1_XMM;

  if (RES->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vmulss(rd, ra, rb);
    } else {
      if (rd != ra) {
        e.movss(rd, ra);
      }
      e.mulss(rd, rb);
    }
  } else {
    if (X64_USE_AVX) {
      e.vmulsd(rd, ra, rb);
    } else {
      if (rd != ra) {
        e.movsd(rd, ra);
      }
      e.mulsd(rd, rb);
    }
  }
}

EMITTER(FDIV, CONSTRAINTS(REG_F64, REG_F64, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;
  Xbyak::Xmm rb = ARG1_XMM;

  if (RES->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vdivss(rd, ra, rb);
    } else {
      if (rd != ra) {
        e.movss(rd, ra);
      }
      e.divss(rd, rb);
    }
  } else {
    if (X64_USE_AVX) {
      e.vdivsd(rd, ra, rb);
    } else {
      if (rd != ra) {
        e.movsd(rd, ra);
      }
      e.divsd(rd, rb);
    }
  }
}

EMITTER(FNEG, CONSTRAINTS(REG_F64, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  if (RES->type == VALUE_F32) {
    Xbyak::Address mask =
        x64_backend_xmm_constant(backend, XMM_CONST_PS_SIGN_MASK);

    if (X64_USE_AVX) {
      e.vxorps(rd, ra, mask);
    } else {
      if (rd != ra) {
        e.movss(rd, ra);
      }
      e.xorps(rd, mask);
    }
  } else {
    Xbyak::Address mask =
        x64_backend_xmm_constant(backend, XMM_CONST_PD_SIGN_MASK);

    if (X64_USE_AVX) {
      e.vxorpd(rd, ra, mask);
    } else {
      if (rd != ra) {
        e.movsd(rd, ra);
      }
      e.xorpd(rd, mask);
    }
  }
}

EMITTER(FABS, CONSTRAINTS(REG_F64, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  if (RES->type == VALUE_F32) {
    Xbyak::Address mask =
        x64_backend_xmm_constant(backend, XMM_CONST_PS_ABS_MASK);

    if (X64_USE_AVX) {
      e.vandps(rd, ra, mask);
    } else {
      if (rd != ra) {
        e.movss(rd, ra);
      }
      e.andps(rd, mask);
    }
  } else {
    Xbyak::Address mask =
        x64_backend_xmm_constant(backend, XMM_CONST_PD_ABS_MASK);

    if (X64_USE_AVX) {
      e.vandpd(rd, ra, mask);
    } else {
      if (rd != ra) {
        e.movsd(rd, ra);
      }
      e.andpd(rd, mask);
    }
  }
}

EMITTER(SQRT, CONSTRAINTS(REG_F64, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  if (RES->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vsqrtss(rd, ra);
    } else {
      e.sqrtss(rd, ra);
    }
  } else {
    if (X64_USE_AVX) {
      e.vsqrtsd(rd, ra);
    } else {
      e.sqrtsd(rd, ra);
    }
  }
}

EMITTER(VBROADCAST, CONSTRAINTS(REG_V128, REG_F64)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  if (X64_USE_AVX) {
    e.vbroadcastss(rd, ra);
  } else {
    e.movss(rd, ra);
    e.shufps(rd, rd, 0);
  }
}

EMITTER(VADD, CONSTRAINTS(REG_V128, REG_V128, REG_V128)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;
  Xbyak::Xmm rb = ARG1_XMM;

  if (X64_USE_AVX) {
    e.vaddps(rd, ra, rb);
  } else {
    if (rd != ra) {
      e.movaps(rd, ra);
    }
    e.addps(rd, rb);
  }
}

EMITTER(VDOT, CONSTRAINTS(REG_V128, REG_V128, REG_V128)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;
  Xbyak::Xmm rb = ARG1_XMM;

  if (X64_USE_AVX) {
    e.vdpps(rd, ra, rb, 0b11110001);
  } else {
    if (rd != ra) {
      e.movaps(rd, ra);
    }
    e.mulps(rd, rb);
    e.haddps(rd, rd);
    e.haddps(rd, rd);
  }
}

EMITTER(VMUL, CONSTRAINTS(REG_V128, REG_V128, REG_V128)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;
  Xbyak::Xmm rb = ARG1_XMM;

  if (X64_USE_AVX) {
    e.vmulps(rd, ra, rb);
  } else {
    if (rd != ra) {
      e.movaps(rd, ra);
    }
    e.mulps(rd, rb);
  }
}

EMITTER(AND, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64 | IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.and_(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.and_(rd, rb);
  }
}

EMITTER(OR, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64 | IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.or_(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.or_(rd, rb);
  }
}

EMITTER(XOR, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64 | IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.xor_(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.xor_(rd, rb);
  }
}

EMITTER(NOT, CONSTRAINTS(REG_ARG0, REG_I64)) {
  Xbyak::Reg rd = RES_REG;

  e.not_(rd);
}

EMITTER(SHL, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64 | IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.shl(rd, (int)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.mov(e.cl, rb);
    e.shl(rd, e.cl);
  }
}

EMITTER(ASHR, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64 | IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.sar(rd, (int)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.mov(e.cl, rb);
    e.sar(rd, e.cl);
  }
}

EMITTER(LSHR, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64 | IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.shr(rd, (int)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.mov(e.cl, rb);
    e.shr(rd, e.cl);
  }
}

EMITTER(ASHD, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg rb = ARG1_REG;

  e.inLocalLabel();

  /* check if we're shifting left or right */
  e.test(rb, 0x80000000);
  e.jnz(".shr");

  /* perform shift left */
  e.mov(e.cl, rb);
  e.sal(rd, e.cl);
  e.jmp(".end");

  /* perform right shift */
  e.L(".shr");
  e.test(rb, 0x1f);
  e.jz(".shr_overflow");
  e.mov(e.cl, rb);
  e.neg(e.cl);
  e.sar(rd, e.cl);
  e.jmp(".end");

  /* right shift overflowed */
  e.L(".shr_overflow");
  e.sar(rd, 31);

  /* shift is done */
  e.L(".end");

  e.outLocalLabel();
}

EMITTER(LSHD, CONSTRAINTS(REG_ARG0, REG_I64, REG_I64)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg rb = ARG1_REG;

  e.inLocalLabel();

  /* check if we're shifting left or right */
  e.test(rb, 0x80000000);
  e.jnz(".shr");

  /* perform shift left */
  e.mov(e.cl, rb);
  e.shl(rd, e.cl);
  e.jmp(".end");

  /* perform right shift */
  e.L(".shr");
  e.test(rb, 0x1f);
  e.jz(".shr_overflow");
  e.mov(e.cl, rb);
  e.neg(e.cl);
  e.shr(rd, e.cl);
  e.jmp(".end");

  /* right shift overflowed */
  e.L(".shr_overflow");
  e.mov(rd, 0x0);

  /* shift is done */
  e.L(".end");

  e.outLocalLabel();
}

EMITTER(BRANCH, CONSTRAINTS(NONE, REG_I64 | IMM_I32 | IMM_BLK)) {
  x64_backend_emit_branch(backend, ir, ARG0);
}

EMITTER(BRANCH_COND, CONSTRAINTS(NONE, REG_I64 | IMM_I32 | IMM_BLK,
                                 REG_I64 | IMM_I32 | IMM_BLK, REG_I64)) {
  struct jit_guest *guest = backend->base.guest;

  Xbyak::Reg cond = ARG2_REG;
  Xbyak::Label next;
  e.test(cond, cond);
  e.jz(next);
  x64_backend_emit_branch(backend, ir, ARG0);
  e.L(next);
  x64_backend_emit_branch(backend, ir, ARG1);
}

EMITTER(CALL, CONSTRAINTS(NONE, VAL_I64, OPT_I64, OPT_I64)) {
  if (ARG1) {
    x64_backend_mov_value(backend, arg0, ARG1);
  }
  if (ARG2) {
    x64_backend_mov_value(backend, arg1, ARG2);
  }

  if (ir_is_constant(ARG0)) {
    void *addr = (void *)ARG0->i64;
    e.call(addr);
  } else {
    Xbyak::Reg addr = ARG0_REG;
    e.call(addr);
  }
}

EMITTER(CALL_COND, CONSTRAINTS(NONE, VAL_I64, VAL_I64, OPT_I64, OPT_I64)) {
  e.inLocalLabel();

  Xbyak::Reg cond = ARG1_REG;

  e.test(cond, cond);

  e.jz(".skip");

  if (ARG2) {
    x64_backend_mov_value(backend, arg0, ARG2);
  }
  if (ARG3) {
    x64_backend_mov_value(backend, arg1, ARG3);
  }

  if (ir_is_constant(ARG0)) {
    void *addr = (void *)ARG0->i64;
    e.call(addr);
  } else {
    const Xbyak::Reg addr = ARG0_REG;
    e.call(addr);
  }

  e.L(".skip");

  e.outLocalLabel();
}

EMITTER(DEBUG_BREAK, CONSTRAINTS(NONE)) {
  e.db(0xcc);
}

static void debug_log(uint64_t a, uint64_t b, uint64_t c) {
  LOG_INFO("DEBUG_LOG a=0x%" PRIx64 " b=0x%" PRIx64 " c=0x%" PRIx64, a, b, c);
}

EMITTER(DEBUG_LOG, CONSTRAINTS(NONE, VAL_I64, OPT_I64, OPT_I64)) {
  x64_backend_mov_value(backend, arg0, ARG0);
  x64_backend_mov_value(backend, arg1, ARG1);
  x64_backend_mov_value(backend, arg2, ARG2);
  e.call(debug_log);
}

EMITTER(ASSERT_EQ, CONSTRAINTS(NONE, REG_I64, REG_I64)) {
  Xbyak::Reg ra = ARG0_REG;
  Xbyak::Reg rb = ARG1_REG;

  e.inLocalLabel();
  e.cmp(ra, rb);
  e.je(".skip");
  e.db(0xcc);
  e.L(".skip");
  e.outLocalLabel();
}

EMITTER(ASSERT_LT, CONSTRAINTS(NONE, REG_I64, REG_I64)) {
  Xbyak::Reg ra = ARG0_REG;
  Xbyak::Reg rb = ARG1_REG;

  e.inLocalLabel();
  e.cmp(ra, rb);
  e.jl(".skip");
  e.db(0xcc);
  e.L(".skip");
  e.outLocalLabel();
}

EMITTER(COPY, CONSTRAINTS(REG_ALL, VAL_ALL)) {
  if (ir_is_float(RES->type)) {
    Xbyak::Xmm rd = RES_XMM;

    if (ir_is_constant(ARG0)) {
      /* copy constant into reg */
      if (ARG0->type == VALUE_F32) {
        e.mov(e.eax, *(int32_t *)&ARG0->f32);

        if (X64_USE_AVX) {
          e.vmovd(rd, e.eax);
        } else {
          e.movd(rd, e.eax);
        }
      } else {
        e.mov(e.rax, *(int64_t *)&ARG0->f64);

        if (X64_USE_AVX) {
          e.vmovq(rd, e.rax);
        } else {
          e.movq(rd, e.rax);
        }
      }
    } else {
      /* copy reg to reg */
      const Xbyak::Xmm rn = ARG0_XMM;

      if (X64_USE_AVX) {
        e.vmovapd(rd, rn);
      } else {
        e.movapd(rd, rn);
      }
    }
  } else {
    Xbyak::Reg rd = RES_REG;

    if (ir_is_constant(ARG0)) {
      /* copy constant into reg */
      e.mov(rd, ir_zext_constant(ARG0));
    } else {
      /* copy reg to reg */
      const Xbyak::Reg rn = ARG0_REG;
      e.mov(rd, rn);
    }
  }
}
