#include "jit/backend/x64/x64_local.h"

extern "C" {
#include "jit/ir/ir.h"
#include "jit/jit.h"
}

enum {
  NONE = JIT_CONSTRAINT_NONE,
  IMM_I32 = JIT_CONSTRAINT_IMM_I32,
  IMM_I64 = JIT_CONSTRAINT_IMM_I64,
  RES_HAS_ARG0 = JIT_CONSTRAINT_RES_HAS_ARG0,
};

struct jit_emitter x64_emitters[IR_NUM_OPS];

#define EMITTER(op, constraints)                                           \
  void x64_emit_##op(struct x64_backend *, Xbyak::CodeGenerator &,         \
                     const struct ir_instr *);                             \
  static struct _x64_##op##_init {                                         \
    _x64_##op##_init() {                                                   \
      x64_emitters[OP_##op] = {(void *)&x64_emit_##op, constraints};       \
    }                                                                      \
  } x64_##op##_init;                                                       \
  void x64_emit_##op(struct x64_backend *backend, Xbyak::CodeGenerator &e, \
                     const struct ir_instr *instr)

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

EMITTER(FALLBACK, CONSTRAINTS(NONE, IMM_I64, IMM_I32, IMM_I32)) {
  struct jit_guest *guest = backend->base.jit->guest;
  void *fallback = (void *)ARG0->i64;
  uint32_t addr = ARG1->i32;
  uint32_t raw_instr = ARG2->i32;

  e.mov(arg0, (uint64_t)guest);
  e.mov(arg1, addr);
  e.mov(arg2, raw_instr);
  e.call(fallback);
}

EMITTER(LOAD_HOST, CONSTRAINTS(NONE, NONE)) {
  struct ir_value *dst = RES;
  Xbyak::Reg src = ARG0_REG;

  x64_backend_load_mem(backend, dst, src);
}

EMITTER(STORE_HOST, CONSTRAINTS(NONE, NONE, NONE)) {
  Xbyak::Reg dst = ARG0_REG;
  struct ir_value *data = ARG1;

  x64_backend_store_mem(backend, dst, data);
}

EMITTER(LOAD_GUEST, CONSTRAINTS(NONE, IMM_I32)) {
  struct jit_guest *guest = backend->base.jit->guest;
  Xbyak::Reg dst = RES_REG;
  struct ir_value *addr = ARG0;

  if (ir_is_constant(addr)) {
    /* peel away one layer of abstraction and directly access the backing
       memory or directly invoke the callback when the address is constant */
    void *ptr;
    void *userdata;
    mem_read_cb read;
    uint32_t offset;
    guest->lookup(guest->space, addr->i32, &ptr, &userdata, &read, NULL,
                  &offset);

    if (ptr) {
      e.mov(e.rax, (uint64_t)ptr);
      x64_backend_load_mem(backend, RES, e.rax);
    } else {
      int data_size = ir_type_size(RES->type);
      uint32_t data_mask = (1 << (data_size * 8)) - 1;

      e.mov(arg0, reinterpret_cast<uint64_t>(userdata));
      e.mov(arg1, offset);
      e.mov(arg2, data_mask);
      e.call(reinterpret_cast<void *>(read));
      e.mov(dst, e.rax);
    }
  } else {
    Xbyak::Reg ra = x64_backend_reg(backend, addr);

    void *fn = nullptr;
    switch (RES->type) {
      case VALUE_I8:
        fn = reinterpret_cast<void *>(guest->r8);
        break;
      case VALUE_I16:
        fn = reinterpret_cast<void *>(guest->r16);
        break;
      case VALUE_I32:
        fn = reinterpret_cast<void *>(guest->r32);
        break;
      case VALUE_I64:
        fn = reinterpret_cast<void *>(guest->r64);
        break;
      default:
        LOG_FATAL("unexpected load result type");
        break;
    }

    e.mov(arg0, reinterpret_cast<uint64_t>(guest->space));
    e.mov(arg1, ra);
    e.call(reinterpret_cast<void *>(fn));
    e.mov(dst, e.rax);
  }
}

EMITTER(STORE_GUEST, CONSTRAINTS(NONE, IMM_I32, NONE)) {
  struct jit_guest *guest = backend->base.jit->guest;
  struct ir_value *addr = ARG0;
  struct ir_value *data = ARG1;

  /* FIXME support IMM data */

  if (ir_is_constant(addr)) {
    /* peel away one layer of abstraction and directly access the backing
       memory or directly invoke the callback when the address is constant */
    void *ptr;
    void *userdata;
    mem_write_cb write;
    uint32_t offset;
    guest->lookup(guest->space, addr->i32, &ptr, &userdata, NULL, &write,
                  &offset);

    if (ptr) {
      e.mov(e.rax, (uint64_t)ptr);
      x64_backend_store_mem(backend, e.rax, data);
    } else {
      Xbyak::Reg rb = x64_backend_reg(backend, data);
      int data_size = ir_type_size(data->type);
      uint32_t data_mask = (1 << (data_size * 8)) - 1;

      e.mov(arg0, reinterpret_cast<uint64_t>(userdata));
      e.mov(arg1, offset);
      e.mov(arg2, rb);
      e.mov(arg3, data_mask);
      e.call(reinterpret_cast<void *>(write));
    }
  } else {
    Xbyak::Reg ra = x64_backend_reg(backend, addr);
    Xbyak::Reg rb = x64_backend_reg(backend, data);

    void *fn = nullptr;
    switch (data->type) {
      case VALUE_I8:
        fn = reinterpret_cast<void *>(guest->w8);
        break;
      case VALUE_I16:
        fn = reinterpret_cast<void *>(guest->w16);
        break;
      case VALUE_I32:
        fn = reinterpret_cast<void *>(guest->w32);
        break;
      case VALUE_I64:
        fn = reinterpret_cast<void *>(guest->w64);
        break;
      default:
        LOG_FATAL("Unexpected store value type");
        break;
    }

    e.mov(arg0, reinterpret_cast<uint64_t>(guest->space));
    e.mov(arg1, ra);
    e.mov(arg2, rb);
    e.call(reinterpret_cast<void *>(fn));
  }
}

EMITTER(LOAD_FAST, CONSTRAINTS(NONE, NONE)) {
  struct ir_value *dst = RES;
  Xbyak::Reg addr = ARG0_REG;

  x64_backend_load_mem(backend, dst, addr.cvt64() + guestmem);
}

EMITTER(STORE_FAST, CONSTRAINTS(NONE, NONE, NONE)) {
  Xbyak::Reg addr = ARG0_REG;
  struct ir_value *data = ARG1;

  /* TODO support IMM data */

  x64_backend_store_mem(backend, addr.cvt64() + guestmem, data);
}

EMITTER(LOAD_CONTEXT, CONSTRAINTS(NONE, IMM_I32)) {
  struct ir_value *dst = RES;
  int offset = ARG0->i32;

  x64_backend_load_mem(backend, dst, guestctx + offset);
}

EMITTER(STORE_CONTEXT, CONSTRAINTS(NONE, IMM_I32, IMM_I64)) {
  int offset = ARG0->i32;
  struct ir_value *data = ARG1;

  if (ir_is_constant(data)) {
    switch (data->type) {
      case VALUE_I8:
        e.mov(e.byte[guestctx + offset], data->i8);
        break;
      case VALUE_I16:
        e.mov(e.word[guestctx + offset], data->i16);
        break;
      case VALUE_I32:
      case VALUE_F32:
        e.mov(e.dword[guestctx + offset], data->i32);
        break;
      case VALUE_I64:
      case VALUE_F64:
        e.mov(e.qword[guestctx + offset], data->i64);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    x64_backend_store_mem(backend, guestctx + offset, data);
  }
}

EMITTER(LOAD_LOCAL, CONSTRAINTS(NONE, IMM_I32)) {
  struct ir_value *dst = RES;
  int offset = X64_STACK_OFFSET_LOCALS + ARG0->i32;

  x64_backend_load_mem(backend, dst, e.rsp + offset);
}

EMITTER(STORE_LOCAL, CONSTRAINTS(NONE, IMM_I32, NONE)) {
  int offset = X64_STACK_OFFSET_LOCALS + ARG0->i32;
  struct ir_value *data = ARG1;

  x64_backend_store_mem(backend, e.rsp + offset, data);
}

EMITTER(FTOI, CONSTRAINTS(NONE, NONE)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Xmm ra = ARG0_XMM;

  switch (RES->type) {
    case VALUE_I32:
      CHECK_EQ(ARG0->type, VALUE_F32);
      e.cvttss2si(rd, ra);
      break;
    case VALUE_I64:
      CHECK_EQ(ARG0->type, VALUE_F64);
      e.cvttsd2si(rd, ra);
      break;
    default:
      LOG_FATAL("Unexpected result type");
      break;
  }
}

EMITTER(ITOF, CONSTRAINTS(NONE, NONE)) {
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
      LOG_FATAL("Unexpected result type");
      break;
  }
}

EMITTER(SEXT, CONSTRAINTS(NONE, NONE)) {
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

EMITTER(ZEXT, CONSTRAINTS(NONE, NONE)) {
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

EMITTER(TRUNC, CONSTRAINTS(NONE, NONE)) {
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
      LOG_FATAL("Unexpected value type");
  }

  if (ra.isBit(32)) {
    /* mov will automatically zero fill the upper 32-bits */
    e.mov(rd, ra);
  } else {
    e.movzx(rd.cvt32(), ra);
  }
}

EMITTER(FEXT, CONSTRAINTS(NONE, NONE)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  e.cvtss2sd(rd, ra);
}

EMITTER(FTRUNC, CONSTRAINTS(NONE, NONE)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  e.cvtsd2ss(rd, ra);
}

EMITTER(SELECT, CONSTRAINTS(NONE, NONE, NONE, NONE)) {
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

EMITTER(CMP, CONSTRAINTS(NONE, NONE, IMM_I32, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg ra = ARG0_REG;

  if (ir_is_constant(ARG1)) {
    e.cmp(ra, static_cast<uint32_t>(ir_zext_constant(ARG1)));
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

EMITTER(FCMP, CONSTRAINTS(NONE, NONE, NONE, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Xmm ra = ARG0_XMM;
  Xbyak::Xmm rb = ARG1_XMM;

  if (ARG0->type == VALUE_F32) {
    e.comiss(ra, rb);
  } else {
    e.comisd(ra, rb);
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

EMITTER(ADD, CONSTRAINTS(RES_HAS_ARG0, NONE, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.add(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.add(rd, rb);
  }
}

EMITTER(SUB, CONSTRAINTS(RES_HAS_ARG0, NONE, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.sub(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.sub(rd, rb);
  }
}

EMITTER(SMUL, CONSTRAINTS(RES_HAS_ARG0, NONE, NONE)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg rb = ARG1_REG;

  e.imul(rd, rb);
}

EMITTER(UMUL, CONSTRAINTS(RES_HAS_ARG0, NONE, NONE)) {
  Xbyak::Reg rd = RES_REG;
  Xbyak::Reg rb = ARG1_REG;

  e.imul(rd, rb);
}

EMITTER(DIV, CONSTRAINTS(NONE)) {
  LOG_FATAL("unsupported");
}

EMITTER(NEG, CONSTRAINTS(RES_HAS_ARG0, NONE)) {
  Xbyak::Reg rd = RES_REG;

  e.neg(rd);
}

EMITTER(ABS, CONSTRAINTS(NONE)) {
  /* e.mov(e.rax, *result);
     e.neg(e.rax);
     e.cmovl(reinterpret_cast<const Xbyak::Reg *>(result)->cvt32(), e.rax); */
  LOG_FATAL("unsupported");
}

EMITTER(FADD, CONSTRAINTS(NONE, NONE, NONE)) {
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

EMITTER(FSUB, CONSTRAINTS(NONE, NONE, NONE)) {
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

EMITTER(FMUL, CONSTRAINTS(NONE)) {
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

EMITTER(FDIV, CONSTRAINTS(NONE)) {
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

EMITTER(FNEG, CONSTRAINTS(NONE)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  if (RES->type == VALUE_F32) {
    Xbyak::Address mask =
        x64_backend_xmm_constant(backend, XMM_CONST_SIGN_MASK_PS);

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
        x64_backend_xmm_constant(backend, XMM_CONST_SIGN_MASK_PD);

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

EMITTER(FABS, CONSTRAINTS(NONE)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  if (RES->type == VALUE_F32) {
    Xbyak::Address mask =
        x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PS);

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
        x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PD);

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

EMITTER(SQRT, CONSTRAINTS(NONE)) {
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

EMITTER(VBROADCAST, CONSTRAINTS(NONE)) {
  Xbyak::Xmm rd = RES_XMM;
  Xbyak::Xmm ra = ARG0_XMM;

  if (X64_USE_AVX) {
    e.vbroadcastss(rd, ra);
  } else {
    e.movss(rd, ra);
    e.shufps(rd, rd, 0);
  }
}

EMITTER(VADD, CONSTRAINTS(NONE)) {
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

EMITTER(VDOT, CONSTRAINTS(NONE)) {
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

EMITTER(VMUL, CONSTRAINTS(NONE)) {
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

EMITTER(AND, CONSTRAINTS(RES_HAS_ARG0, NONE, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.and_(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.and_(rd, rb);
  }
}

EMITTER(OR, CONSTRAINTS(RES_HAS_ARG0, NONE, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.or_(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.or_(rd, rb);
  }
}

EMITTER(XOR, CONSTRAINTS(RES_HAS_ARG0, NONE, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.xor_(rd, (uint32_t)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.xor_(rd, rb);
  }
}

EMITTER(NOT, CONSTRAINTS(RES_HAS_ARG0, NONE)) {
  Xbyak::Reg rd = RES_REG;

  e.not_(rd);
}

EMITTER(SHL, CONSTRAINTS(RES_HAS_ARG0, NONE, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.shl(rd, (int)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.mov(e.cl, rb);
    e.shl(rd, e.cl);
  }
}

EMITTER(ASHR, CONSTRAINTS(RES_HAS_ARG0, NONE, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.sar(rd, (int)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.mov(e.cl, rb);
    e.sar(rd, e.cl);
  }
}

EMITTER(LSHR, CONSTRAINTS(RES_HAS_ARG0, NONE, IMM_I32)) {
  Xbyak::Reg rd = RES_REG;

  if (ir_is_constant(ARG1)) {
    e.shr(rd, (int)ir_zext_constant(ARG1));
  } else {
    Xbyak::Reg rb = ARG1_REG;
    e.mov(e.cl, rb);
    e.shr(rd, e.cl);
  }
}

EMITTER(ASHD, CONSTRAINTS(RES_HAS_ARG0)) {
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

EMITTER(LSHD, CONSTRAINTS(RES_HAS_ARG0)) {
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

EMITTER(BRANCH, CONSTRAINTS(NONE, IMM_I32)) {
  struct jit_guest *guest = backend->base.jit->guest;

  if (ir_is_constant(ARG0)) {
    uint32_t addr = ARG0->i32;
    e.mov(e.dword[guestctx + guest->offset_pc], addr);
    e.call(backend->dispatch_static);
  } else {
    Xbyak::Reg addr = ARG0_REG;
    e.mov(e.dword[guestctx + guest->offset_pc], addr);
    e.jmp(backend->dispatch_dynamic);
  }
}

EMITTER(BRANCH_FALSE, CONSTRAINTS(NONE, IMM_I32)) {
  struct jit_guest *guest = backend->base.jit->guest;

  e.inLocalLabel();

  Xbyak::Reg cond = ARG1_REG;
  e.test(cond, cond);
  e.jnz(".next");

  if (ir_is_constant(ARG0)) {
    uint32_t addr = ARG0->i32;
    e.mov(e.dword[guestctx + guest->offset_pc], addr);
    e.call(backend->dispatch_static);
  } else {
    Xbyak::Reg addr = ARG0_REG;
    e.mov(e.dword[guestctx + guest->offset_pc], addr);
    e.jmp(backend->dispatch_dynamic);
  }

  e.L(".next");

  e.outLocalLabel();
}

EMITTER(BRANCH_TRUE, CONSTRAINTS(NONE, IMM_I32)) {
  struct jit_guest *guest = backend->base.jit->guest;

  e.inLocalLabel();

  const Xbyak::Reg cond = ARG1_REG;
  e.test(cond, cond);
  e.jz(".next");

  if (ir_is_constant(ARG0)) {
    uint32_t addr = ARG0->i32;
    e.mov(e.dword[guestctx + guest->offset_pc], addr);
    e.call(backend->dispatch_static);
  } else {
    const Xbyak::Reg addr = ARG0_REG;
    e.mov(e.dword[guestctx + guest->offset_pc], addr);
    e.jmp(backend->dispatch_dynamic);
  }

  e.L(".next");

  e.outLocalLabel();
}

EMITTER(CALL, CONSTRAINTS(NONE, IMM_I64)) {
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

EMITTER(CALL_COND, CONSTRAINTS(NONE, IMM_I64)) {
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

EMITTER(ASSERT_LT, CONSTRAINTS(NONE)) {
  Xbyak::Reg ra = ARG0_REG;
  Xbyak::Reg rb = ARG1_REG;

  e.inLocalLabel();
  e.cmp(ra, rb);
  e.jl(".skip");
  e.db(0xcc);
  e.L(".skip");
  e.outLocalLabel();
}

EMITTER(COPY, CONSTRAINTS(NONE, IMM_I64)) {
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
