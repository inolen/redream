#include "jit/backend/x64/x64_local.h"

extern "C" {
#include "jit/ir/ir.h"
#include "jit/jit.h"
}

x64_emit_cb x64_backend_emitters[NUM_OPS];

#define EMITTER(op)                                                        \
  void x64_emit_##op(struct x64_backend *, Xbyak::CodeGenerator &,         \
                     const struct ir_instr *);                             \
  static struct _x64_##op##_init {                                         \
    _x64_##op##_init() {                                                   \
      x64_backend_emitters[OP_##op] = &x64_emit_##op;                      \
    }                                                                      \
  } x64_##op##_init;                                                       \
  void x64_emit_##op(struct x64_backend *backend, Xbyak::CodeGenerator &e, \
                     const struct ir_instr *instr)

EMITTER(FALLBACK) {
  struct jit_guest *guest = backend->base.jit->guest;
  void *fallback = (void *)instr->arg[0]->i64;
  uint32_t addr = instr->arg[1]->i32;
  uint32_t raw_instr = instr->arg[2]->i32;

  e.mov(arg0, (uint64_t)guest);
  e.mov(arg1, addr);
  e.mov(arg2, raw_instr);
  e.call(fallback);
}

EMITTER(LOAD_HOST) {
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  x64_backend_load_mem(backend, instr->result, a);
}

EMITTER(STORE_HOST) {
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  x64_backend_store_mem(backend, a, instr->arg[1]);
}

EMITTER(LOAD_GUEST) {
  struct jit_guest *guest = backend->base.jit->guest;
  struct ir_value *result = instr->result;
  struct ir_value *addr = instr->arg[0];
  const Xbyak::Reg d = x64_backend_reg(backend, result);

  if (ir_is_constant(addr)) {
    /* try to either use fastmem or directly invoke the MMIO callback */
    void *ptr;
    void *userdata;
    mem_read_cb read;
    uint32_t offset;
    guest->lookup(guest->space, addr->i32, &ptr, &userdata, &read, NULL,
                  &offset);

    if (ptr) {
      const Xbyak::Reg a = x64_backend_reg(backend, addr);

      x64_backend_load_mem(backend, result, a.cvt64() + guestmem);
    } else {
      int data_size = ir_type_size(result->type);
      uint32_t data_mask = (1 << (data_size * 8)) - 1;

      e.mov(arg0, reinterpret_cast<uint64_t>(userdata));
      e.mov(arg1, offset);
      e.mov(arg2, data_mask);
      e.call(reinterpret_cast<void *>(read));
      e.mov(d, e.rax);
    }
  } else {
    const Xbyak::Reg a = x64_backend_reg(backend, addr);

    void *fn = nullptr;
    switch (result->type) {
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
        LOG_FATAL("Unexpected load result type");
        break;
    }

    e.mov(arg0, reinterpret_cast<uint64_t>(guest->space));
    e.mov(arg1, a);
    e.call(reinterpret_cast<void *>(fn));
    e.mov(d, e.rax);
  }
}

EMITTER(STORE_GUEST) {
  struct jit_guest *guest = backend->base.jit->guest;
  struct ir_value *addr = instr->arg[0];
  struct ir_value *data = instr->arg[1];

  if (ir_is_constant(addr)) {
    /* try to either use fastmem or directly invoke the MMIO callback */
    void *ptr;
    void *userdata;
    mem_write_cb write;
    uint32_t offset;
    guest->lookup(guest->space, addr->i32, &ptr, &userdata, NULL, &write,
                  &offset);

    if (ptr) {
      const Xbyak::Reg a = x64_backend_reg(backend, addr);

      x64_backend_store_mem(backend, a.cvt64() + guestmem, data);
    } else {
      const Xbyak::Reg b = x64_backend_reg(backend, data);
      int data_size = ir_type_size(data->type);
      uint32_t data_mask = (1 << (data_size * 8)) - 1;

      e.mov(arg0, reinterpret_cast<uint64_t>(userdata));
      e.mov(arg1, offset);
      e.mov(arg2, b);
      e.mov(arg3, data_mask);
      e.call(reinterpret_cast<void *>(write));
    }
  } else {
    const Xbyak::Reg a = x64_backend_reg(backend, addr);
    const Xbyak::Reg b = x64_backend_reg(backend, data);

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
    e.mov(arg1, a);
    e.mov(arg2, b);
    e.call(reinterpret_cast<void *>(fn));
  }
}

EMITTER(LOAD_FAST) {
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  x64_backend_load_mem(backend, instr->result, a.cvt64() + guestmem);
}

EMITTER(STORE_FAST) {
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  x64_backend_store_mem(backend, a.cvt64() + guestmem, instr->arg[1]);
}

EMITTER(LOAD_CONTEXT) {
  int offset = instr->arg[0]->i32;

  x64_backend_load_mem(backend, instr->result, guestctx + offset);
}

EMITTER(STORE_CONTEXT) {
  int offset = instr->arg[0]->i32;

  if (ir_is_constant(instr->arg[1])) {
    switch (instr->arg[1]->type) {
      case VALUE_I8:
        e.mov(e.byte[guestctx + offset], instr->arg[1]->i8);
        break;
      case VALUE_I16:
        e.mov(e.word[guestctx + offset], instr->arg[1]->i16);
        break;
      case VALUE_I32:
      case VALUE_F32:
        e.mov(e.dword[guestctx + offset], instr->arg[1]->i32);
        break;
      case VALUE_I64:
      case VALUE_F64:
        e.mov(e.qword[guestctx + offset], instr->arg[1]->i64);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    x64_backend_store_mem(backend, guestctx + offset, instr->arg[1]);
  }
}

EMITTER(LOAD_LOCAL) {
  int offset = X64_STACK_OFFSET_LOCALS + instr->arg[0]->i32;

  x64_backend_load_mem(backend, instr->result, e.rsp + offset);
}

EMITTER(STORE_LOCAL) {
  int offset = X64_STACK_OFFSET_LOCALS + instr->arg[0]->i32;

  CHECK(!ir_is_constant(instr->arg[1]));

  x64_backend_store_mem(backend, e.rsp + offset, instr->arg[1]);
}

EMITTER(FTOI) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);

  switch (instr->result->type) {
    case VALUE_I32:
      CHECK_EQ(instr->arg[0]->type, VALUE_F32);
      e.cvttss2si(result, a);
      break;
    case VALUE_I64:
      CHECK_EQ(instr->arg[0]->type, VALUE_F64);
      e.cvttsd2si(result, a);
      break;
    default:
      LOG_FATAL("Unexpected result type");
      break;
  }
}

EMITTER(ITOF) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  switch (instr->result->type) {
    case VALUE_F32:
      CHECK_EQ(instr->arg[0]->type, VALUE_I32);
      e.cvtsi2ss(result, a);
      break;
    case VALUE_F64:
      CHECK_EQ(instr->arg[0]->type, VALUE_I64);
      e.cvtsi2sd(result, a);
      break;
    default:
      LOG_FATAL("Unexpected result type");
      break;
  }
}

EMITTER(SEXT) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (a == result) {
    /* already the correct width */
    return;
  }

  if (result.isBit(64) && a.isBit(32)) {
    e.movsxd(result.cvt64(), a);
  } else {
    e.movsx(result, a);
  }
}

EMITTER(ZEXT) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (a == result) {
    /* already the correct width */
    return;
  }

  if (result.isBit(64) && a.isBit(32)) {
    /* mov will automatically zero fill the upper 32-bits */
    e.mov(result.cvt32(), a);
  } else {
    e.movzx(result, a);
  }
}

EMITTER(TRUNC) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result.getIdx() == a.getIdx()) {
    /* noop if already the same register. note, this means the high order bits
       of the result won't be cleared, but I believe that is fine */
    return;
  }

  Xbyak::Reg truncated = a;
  switch (instr->result->type) {
    case VALUE_I8:
      truncated = a.cvt8();
      break;
    case VALUE_I16:
      truncated = a.cvt16();
      break;
    case VALUE_I32:
      truncated = a.cvt32();
      break;
    default:
      LOG_FATAL("Unexpected value type");
  }

  if (truncated.isBit(32)) {
    /* mov will automatically zero fill the upper 32-bits */
    e.mov(result, truncated);
  } else {
    e.movzx(result.cvt32(), truncated);
  }
}

EMITTER(FEXT) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);

  e.cvtss2sd(result, a);
}

EMITTER(FTRUNC) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);

  e.cvtsd2ss(result, a);
}

EMITTER(SELECT) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);
  const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
  const Xbyak::Reg cond = x64_backend_reg(backend, instr->arg[2]);

  /* convert result to Reg32e to please xbyak */
  CHECK_GE(result.getBit(), 32);
  Xbyak::Reg32e result_32e(result.getIdx(), result.getBit());

  e.test(cond, cond);
  if (result_32e != a) {
    e.cmovnz(result_32e, a);
  }
  e.cmovz(result_32e, b);
}

EMITTER(CMP) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.cmp(a, static_cast<uint32_t>(ir_zext_constant(instr->arg[1])));
  } else {
    const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
    e.cmp(a, b);
  }

  enum ir_cmp cmp = (enum ir_cmp)instr->arg[2]->i32;

  switch (cmp) {
    case CMP_EQ:
      e.sete(result);
      break;

    case CMP_NE:
      e.setne(result);
      break;

    case CMP_SGE:
      e.setge(result);
      break;

    case CMP_SGT:
      e.setg(result);
      break;

    case CMP_UGE:
      e.setae(result);
      break;

    case CMP_UGT:
      e.seta(result);
      break;

    case CMP_SLE:
      e.setle(result);
      break;

    case CMP_SLT:
      e.setl(result);
      break;

    case CMP_ULE:
      e.setbe(result);
      break;

    case CMP_ULT:
      e.setb(result);
      break;

    default:
      LOG_FATAL("Unexpected comparison type");
  }
}

EMITTER(FCMP) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm(backend, instr->arg[1]);

  if (instr->arg[0]->type == VALUE_F32) {
    e.comiss(a, b);
  } else {
    e.comisd(a, b);
  }

  enum ir_cmp cmp = (enum ir_cmp)instr->arg[2]->i32;

  switch (cmp) {
    case CMP_EQ:
      e.sete(result);
      break;

    case CMP_NE:
      e.setne(result);
      break;

    case CMP_SGE:
      e.setae(result);
      break;

    case CMP_SGT:
      e.seta(result);
      break;

    case CMP_SLE:
      e.setbe(result);
      break;

    case CMP_SLT:
      e.setb(result);
      break;

    default:
      LOG_FATAL("Unexpected comparison type");
  }
}

EMITTER(ADD) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.add(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
    e.add(result, b);
  }
}

EMITTER(SUB) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.sub(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
    e.sub(result, b);
  }
}

EMITTER(SMUL) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);
  const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);

  if (result != a) {
    e.mov(result, a);
  }

  e.imul(result, b);
}

EMITTER(UMUL) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);
  const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);

  if (result != a) {
    e.mov(result, a);
  }

  e.imul(result, b);
}

EMITTER(DIV) {
  LOG_FATAL("Unsupported");
}

EMITTER(NEG) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  e.neg(result);
}

EMITTER(ABS) {
  LOG_FATAL("Unsupported");
  /* e.mov(e.rax, *result);
     e.neg(e.rax);
     e.cmovl(reinterpret_cast<const Xbyak::Reg *>(result)->cvt32(), e.rax); */
}

EMITTER(FADD) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm(backend, instr->arg[1]);

  if (instr->result->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vaddss(result, a, b);
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.addss(result, b);
    }
  } else {
    if (X64_USE_AVX) {
      e.vaddsd(result, a, b);
    } else {
      if (result != a) {
        e.movsd(result, a);
      }
      e.addsd(result, b);
    }
  }
}

EMITTER(FSUB) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm(backend, instr->arg[1]);

  if (instr->result->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vsubss(result, a, b);
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.subss(result, b);
    }
  } else {
    if (X64_USE_AVX) {
      e.vsubsd(result, a, b);
    } else {
      if (result != a) {
        e.movsd(result, a);
      }
      e.subsd(result, b);
    }
  }
}

EMITTER(FMUL) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm(backend, instr->arg[1]);

  if (instr->result->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vmulss(result, a, b);
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.mulss(result, b);
    }
  } else {
    if (X64_USE_AVX) {
      e.vmulsd(result, a, b);
    } else {
      if (result != a) {
        e.movsd(result, a);
      }
      e.mulsd(result, b);
    }
  }
}

EMITTER(FDIV) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm(backend, instr->arg[1]);

  if (instr->result->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vdivss(result, a, b);
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.divss(result, b);
    }
  } else {
    if (X64_USE_AVX) {
      e.vdivsd(result, a, b);
    } else {
      if (result != a) {
        e.movsd(result, a);
      }
      e.divsd(result, b);
    }
  }
}

EMITTER(FNEG) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);

  if (instr->result->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vxorps(result, a,
               x64_backend_xmm_constant(backend, XMM_CONST_SIGN_MASK_PS));
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.xorps(result,
              x64_backend_xmm_constant(backend, XMM_CONST_SIGN_MASK_PS));
    }
  } else {
    if (X64_USE_AVX) {
      e.vxorpd(result, a,
               x64_backend_xmm_constant(backend, XMM_CONST_SIGN_MASK_PD));
    } else {
      if (result != a) {
        e.movsd(result, a);
      }
      e.xorpd(result,
              x64_backend_xmm_constant(backend, XMM_CONST_SIGN_MASK_PD));
    }
  }
}

EMITTER(FABS) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);

  if (instr->result->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vandps(result, a,
               x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PS));
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.andps(result, x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PS));
    }
  } else {
    if (X64_USE_AVX) {
      e.vandpd(result, a,
               x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PD));
    } else {
      if (result != a) {
        e.movsd(result, a);
      }
      e.andpd(result, x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PD));
    }
  }
}

EMITTER(SQRT) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);

  if (instr->result->type == VALUE_F32) {
    if (X64_USE_AVX) {
      e.vsqrtss(result, a);
    } else {
      e.sqrtss(result, a);
    }
  } else {
    if (X64_USE_AVX) {
      e.vsqrtsd(result, a);
    } else {
      e.sqrtsd(result, a);
    }
  }
}

EMITTER(VBROADCAST) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);

  if (X64_USE_AVX) {
    e.vbroadcastss(result, a);
  } else {
    e.movss(result, a);
    e.shufps(result, result, 0);
  }
}

EMITTER(VADD) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm(backend, instr->arg[1]);

  if (X64_USE_AVX) {
    e.vaddps(result, a, b);
  } else {
    if (result != a) {
      e.movaps(result, a);
    }
    e.addps(result, b);
  }
}

EMITTER(VDOT) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm(backend, instr->arg[1]);

  if (X64_USE_AVX) {
    e.vdpps(result, a, b, 0b11110001);
  } else {
    if (result != a) {
      e.movaps(result, a);
    }
    e.mulps(result, b);
    e.haddps(result, result);
    e.haddps(result, result);
  }
}

EMITTER(VMUL) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm(backend, instr->arg[1]);

  if (X64_USE_AVX) {
    e.vmulps(result, a, b);
  } else {
    if (result != a) {
      e.movaps(result, a);
    }
    e.mulps(result, b);
  }
}

EMITTER(AND) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.and_(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
    e.and_(result, b);
  }
}

EMITTER(OR) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.or_(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
    e.or_(result, b);
  }
}

EMITTER(XOR) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.xor_(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
    e.xor_(result, b);
  }
}

EMITTER(NOT) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  e.not_(result);
}

EMITTER(SHL) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.shl(result, (int)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
    e.mov(e.cl, b);
    e.shl(result, e.cl);
  }
}

EMITTER(ASHR) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.sar(result, (int)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
    e.mov(e.cl, b);
    e.sar(result, e.cl);
  }
}

EMITTER(LSHR) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.shr(result, (int)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);
    e.mov(e.cl, b);
    e.shr(result, e.cl);
  }
}

EMITTER(ASHD) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg v = x64_backend_reg(backend, instr->arg[0]);
  const Xbyak::Reg n = x64_backend_reg(backend, instr->arg[1]);

  e.inLocalLabel();

  if (result != v) {
    e.mov(result, v);
  }

  /* check if we're shifting left or right */
  e.test(n, 0x80000000);
  e.jnz(".shr");

  /* perform shift left */
  e.mov(e.cl, n);
  e.sal(result, e.cl);
  e.jmp(".end");

  /* perform right shift */
  e.L(".shr");
  e.test(n, 0x1f);
  e.jz(".shr_overflow");
  e.mov(e.cl, n);
  e.neg(e.cl);
  e.sar(result, e.cl);
  e.jmp(".end");

  /* right shift overflowed */
  e.L(".shr_overflow");
  e.sar(result, 31);

  /* shift is done */
  e.L(".end");

  e.outLocalLabel();
}

EMITTER(LSHD) {
  const Xbyak::Reg result = x64_backend_reg(backend, instr->result);
  const Xbyak::Reg v = x64_backend_reg(backend, instr->arg[0]);
  const Xbyak::Reg n = x64_backend_reg(backend, instr->arg[1]);

  e.inLocalLabel();

  if (result != v) {
    e.mov(result, v);
  }

  /* check if we're shifting left or right */
  e.test(n, 0x80000000);
  e.jnz(".shr");

  /* perform shift left */
  e.mov(e.cl, n);
  e.shl(result, e.cl);
  e.jmp(".end");

  /* perform right shift */
  e.L(".shr");
  e.test(n, 0x1f);
  e.jz(".shr_overflow");
  e.mov(e.cl, n);
  e.neg(e.cl);
  e.shr(result, e.cl);
  e.jmp(".end");

  /* right shift overflowed */
  e.L(".shr_overflow");
  e.mov(result, 0x0);

  /* shift is done */
  e.L(".end");

  e.outLocalLabel();
}

EMITTER(BRANCH) {
  struct jit_guest *guest = backend->base.jit->guest;

  if (ir_is_constant(instr->arg[0])) {
    uint32_t branch_addr = instr->arg[0]->i32;
    e.mov(e.dword[guestctx + guest->offset_pc], branch_addr);
    e.call(backend->dispatch_static);
  } else {
    const Xbyak::Reg branch_addr = x64_backend_reg(backend, instr->arg[0]);
    e.mov(e.dword[guestctx + guest->offset_pc], branch_addr);
    e.jmp(backend->dispatch_dynamic);
  }
}

EMITTER(BRANCH_FALSE) {
  struct jit_guest *guest = backend->base.jit->guest;

  e.inLocalLabel();

  const Xbyak::Reg cond = x64_backend_reg(backend, instr->arg[1]);
  e.test(cond, cond);
  e.jnz(".next");

  if (ir_is_constant(instr->arg[0])) {
    uint32_t branch_addr = instr->arg[0]->i32;
    e.mov(e.dword[guestctx + guest->offset_pc], branch_addr);
    e.call(backend->dispatch_static);
  } else {
    const Xbyak::Reg branch_addr = x64_backend_reg(backend, instr->arg[0]);
    e.mov(e.dword[guestctx + guest->offset_pc], branch_addr);
    e.jmp(backend->dispatch_dynamic);
  }

  e.L(".next");

  e.outLocalLabel();
}

EMITTER(BRANCH_TRUE) {
  struct jit_guest *guest = backend->base.jit->guest;

  e.inLocalLabel();

  const Xbyak::Reg cond = x64_backend_reg(backend, instr->arg[1]);
  e.test(cond, cond);
  e.jz(".next");

  if (ir_is_constant(instr->arg[0])) {
    uint32_t branch_addr = instr->arg[0]->i32;
    e.mov(e.dword[guestctx + guest->offset_pc], branch_addr);
    e.call(backend->dispatch_static);
  } else {
    const Xbyak::Reg branch_addr = x64_backend_reg(backend, instr->arg[0]);
    e.mov(e.dword[guestctx + guest->offset_pc], branch_addr);
    e.jmp(backend->dispatch_dynamic);
  }

  e.L(".next");

  e.outLocalLabel();
}

EMITTER(CALL) {
  if (instr->arg[1]) {
    x64_backend_mov_value(backend, arg0, instr->arg[1]);
  }
  if (instr->arg[2]) {
    x64_backend_mov_value(backend, arg1, instr->arg[2]);
  }

  if (ir_is_constant(instr->arg[0])) {
    e.call((void *)instr->arg[0]->i64);
  } else {
    const Xbyak::Reg addr = x64_backend_reg(backend, instr->arg[0]);
    e.call(addr);
  }
}

EMITTER(CALL_COND) {
  e.inLocalLabel();

  const Xbyak::Reg cond = x64_backend_reg(backend, instr->arg[1]);

  e.test(cond, cond);

  e.jz(".skip");

  if (instr->arg[2]) {
    x64_backend_mov_value(backend, arg0, instr->arg[2]);
  }
  if (instr->arg[3]) {
    x64_backend_mov_value(backend, arg1, instr->arg[3]);
  }

  if (ir_is_constant(instr->arg[0])) {
    e.call((void *)instr->arg[0]->i64);
  } else {
    const Xbyak::Reg addr = x64_backend_reg(backend, instr->arg[0]);
    e.call(addr);
  }

  e.L(".skip");

  e.outLocalLabel();
}

EMITTER(DEBUG_BREAK) {
  e.db(0xcc);
}

EMITTER(ASSERT_LT) {
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);
  const Xbyak::Reg b = x64_backend_reg(backend, instr->arg[1]);

  e.inLocalLabel();
  e.cmp(a, b);
  e.jl(".skip");
  e.db(0xcc);
  e.L(".skip");
  e.outLocalLabel();
}
