#include "jit/backend/x64/x64_local.h"

extern "C" {
#include "core/exception_handler.h"
#include "core/memory.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/backend/x64/x64_disassembler.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "jit/jit_backend.h"
#include "jit/jit_guest.h"
}

/*
 * x64 register layout
 */

/* clang-format off */
#if PLATFORM_WINDOWS
const int x64_arg0_idx = Xbyak::Operand::RCX;
const int x64_arg1_idx = Xbyak::Operand::RDX;
const int x64_arg2_idx = Xbyak::Operand::R8;
const int x64_arg3_idx = Xbyak::Operand::R9;
#else
const int x64_arg0_idx = Xbyak::Operand::RDI;
const int x64_arg1_idx = Xbyak::Operand::RSI;
const int x64_arg2_idx = Xbyak::Operand::RDX;
const int x64_arg3_idx = Xbyak::Operand::RCX;
#endif

const Xbyak::Reg64 arg0(x64_arg0_idx);
const Xbyak::Reg64 arg1(x64_arg1_idx);
const Xbyak::Reg64 arg2(x64_arg2_idx);
const Xbyak::Reg64 arg3(x64_arg3_idx);
const Xbyak::Reg64 guestctx(Xbyak::Operand::R14);
const Xbyak::Reg64 guestmem(Xbyak::Operand::R15);

const struct jit_register x64_registers[] = {
    {"rax",   JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rax},
    {"rcx",   JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rcx},
    {"rdx",   JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rdx},
    {"rbx",   JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rbx},
    {"rsp",   JIT_RESERVED | JIT_CALLEE_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rsp},
    {"rbp",   JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rbp},
#if PLATFORM_WINDOWS
    {"rsi",   JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rsi},
    {"rdi",   JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rdi},
    {"r8",    JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r8},
    {"r9",    JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r9},
#else
    {"rsi",   JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rsi},
    {"rdi",   JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::rdi},
    {"r8",    JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r8},
    {"r9",    JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r9},
#endif
    {"r10",   JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r10},
    {"r11",   JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r11},
    {"r12",   JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r12},
    {"r13",   JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r13},
    {"r14",   JIT_RESERVED | JIT_CALLEE_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r14},
    {"r15",   JIT_RESERVED | JIT_CALLEE_SAVE | JIT_REG_I64,                (const void *)&Xbyak::util::r15},
#if PLATFORM_WINDOWS
    {"xmm0",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm0},
    {"xmm1",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm1},
    {"xmm2",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm2},
    {"xmm3",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm3},
    {"xmm4",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm4},
    {"xmm5",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm5},
    {"xmm6",  JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm6},
    {"xmm7",  JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm7},
    {"xmm8",  JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm8},
    {"xmm9",  JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm9},
    {"xmm10", JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm10},
    {"xmm11", JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm11},
    {"xmm12", JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm12},
    {"xmm13", JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm13},
    {"xmm14", JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm14},
    {"xmm15", JIT_ALLOCATE | JIT_CALLEE_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm15},
#else
    {"xmm0",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm0},
    {"xmm1",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm1},
    {"xmm2",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm2},
    {"xmm3",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm3},
    {"xmm4",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm4},
    {"xmm5",  JIT_RESERVED | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm5},
    {"xmm6",  JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm6},
    {"xmm7",  JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm7},
    {"xmm8",  JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm8},
    {"xmm9",  JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm9},
    {"xmm10", JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm10},
    {"xmm11", JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm11},
    {"xmm12", JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm12},
    {"xmm13", JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm13},
    {"xmm14", JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm14},
    {"xmm15", JIT_ALLOCATE | JIT_CALLER_SAVE | JIT_REG_F64 | JIT_REG_V128, (const void *)&Xbyak::util::xmm15},
#endif
};

const int x64_num_registers = ARRAY_SIZE(x64_registers);
/* clang-format on */

Xbyak::Reg x64_backend_reg(struct x64_backend *backend,
                           const struct ir_value *v) {
  CHECK(v->reg >= 0 && v->reg < x64_num_registers);
  Xbyak::Reg reg = *(const Xbyak::Reg *)x64_registers[v->reg].data;
  CHECK(reg.isREG());

  switch (v->type) {
    case VALUE_I8:
      reg = reg.cvt8();
      break;
    case VALUE_I16:
      reg = reg.cvt16();
      break;
    case VALUE_I32:
      reg = reg.cvt32();
      break;
    case VALUE_I64:
      /* no conversion needed */
      break;
    default:
      LOG_FATAL("unexpected value type");
      break;
  }

  return reg;
}

Xbyak::Xmm x64_backend_xmm(struct x64_backend *backend,
                           const struct ir_value *v) {
  CHECK(v->reg >= 0 && v->reg < x64_num_registers);
  Xbyak::Xmm xmm = *(const Xbyak::Xmm *)x64_registers[v->reg].data;
  CHECK(xmm.isXMM());
  return xmm;
}

int x64_backend_push_regs(struct x64_backend *backend, int mask) {
  int size = 0;

  auto &e = *backend->codegen;

  for (int i = 0; i < x64_num_registers; i++) {
    const struct jit_register *r = &x64_registers[i];

    if ((r->flags & mask) != mask) {
      continue;
    }

    if (r->flags & JIT_REG_I64) {
      Xbyak::Reg reg = *(const Xbyak::Reg *)r->data;
      CHECK(reg.isREG());
      size += 8;
      e.mov(e.qword[e.rsp - size], reg);
    } else if (r->flags & (JIT_REG_F64 | JIT_REG_V128)) {
      Xbyak::Xmm xmm = *(const Xbyak::Xmm *)r->data;
      CHECK(xmm.isXMM());
      size += 16;
      e.movdqu(e.ptr[e.rsp - size], xmm);
    }
  }

  return size;
}

void x64_backend_pop_regs(struct x64_backend *backend, int mask) {
  int size = 0;

  auto &e = *backend->codegen;

  for (int i = 0; i < x64_num_registers; i++) {
    const struct jit_register *r = &x64_registers[i];

    if ((r->flags & mask) != mask) {
      continue;
    }

    if ((r->flags & JIT_REG_I64)) {
      Xbyak::Reg reg = *(const Xbyak::Reg *)r->data;
      CHECK(reg.isREG());
      size += 8;
      e.mov(reg, e.qword[e.rsp - size]);
    } else if (r->flags & (JIT_REG_F64 | JIT_REG_V128)) {
      Xbyak::Xmm xmm = *(const Xbyak::Xmm *)r->data;
      CHECK(xmm.isXMM());
      size += 16;
      e.movdqu(xmm, e.ptr[e.rsp - size]);
    }
  }
}

void x64_backend_load_mem(struct x64_backend *backend,
                          const struct ir_value *dst,
                          const Xbyak::RegExp &src_exp) {
  auto &e = *backend->codegen;

  switch (dst->type) {
    case VALUE_I8:
      e.mov(x64_backend_reg(backend, dst), e.byte[src_exp]);
      break;
    case VALUE_I16:
      e.mov(x64_backend_reg(backend, dst), e.word[src_exp]);
      break;
    case VALUE_I32:
      e.mov(x64_backend_reg(backend, dst), e.dword[src_exp]);
      break;
    case VALUE_I64:
      e.mov(x64_backend_reg(backend, dst), e.qword[src_exp]);
      break;
    case VALUE_F32:
      if (X64_USE_AVX) {
        e.vmovss(x64_backend_xmm(backend, dst), e.dword[src_exp]);
      } else {
        e.movss(x64_backend_xmm(backend, dst), e.dword[src_exp]);
      }
      break;
    case VALUE_F64:
      if (X64_USE_AVX) {
        e.vmovsd(x64_backend_xmm(backend, dst), e.qword[src_exp]);
      } else {
        e.movsd(x64_backend_xmm(backend, dst), e.qword[src_exp]);
      }
      break;
    case VALUE_V128:
      if (X64_USE_AVX) {
        e.vmovups(x64_backend_xmm(backend, dst), e.ptr[src_exp]);
      } else {
        e.movups(x64_backend_xmm(backend, dst), e.ptr[src_exp]);
      }
      break;
    default:
      LOG_FATAL("unexpected load result type");
      break;
  }
}

void x64_backend_store_mem(struct x64_backend *backend,
                           const Xbyak::RegExp &dst_exp,
                           const struct ir_value *src) {
  auto &e = *backend->codegen;

  if (ir_is_constant(src)) {
    switch (src->type) {
      case VALUE_I8:
        e.mov(e.byte[dst_exp], src->i8);
        break;
      case VALUE_I16:
        e.mov(e.word[dst_exp], src->i16);
        break;
      case VALUE_I32:
      case VALUE_F32:
        e.mov(e.dword[dst_exp], src->i32);
        break;
      case VALUE_I64:
      case VALUE_F64:
        e.mov(e.qword[dst_exp], src->i64);
        break;
      default:
        LOG_FATAL("unexpected value type");
        break;
    }
    return;
  }

  switch (src->type) {
    case VALUE_I8:
      e.mov(e.byte[dst_exp], x64_backend_reg(backend, src));
      break;
    case VALUE_I16:
      e.mov(e.word[dst_exp], x64_backend_reg(backend, src));
      break;
    case VALUE_I32:
      e.mov(e.dword[dst_exp], x64_backend_reg(backend, src));
      break;
    case VALUE_I64:
      e.mov(e.qword[dst_exp], x64_backend_reg(backend, src));
      break;
    case VALUE_F32:
      if (X64_USE_AVX) {
        e.vmovss(e.dword[dst_exp], x64_backend_xmm(backend, src));
      } else {
        e.movss(e.dword[dst_exp], x64_backend_xmm(backend, src));
      }
      break;
    case VALUE_F64:
      if (X64_USE_AVX) {
        e.vmovsd(e.qword[dst_exp], x64_backend_xmm(backend, src));
      } else {
        e.movsd(e.qword[dst_exp], x64_backend_xmm(backend, src));
      }
      break;
    case VALUE_V128:
      if (X64_USE_AVX) {
        e.vmovups(e.ptr[dst_exp], x64_backend_xmm(backend, src));
      } else {
        e.movups(e.ptr[dst_exp], x64_backend_xmm(backend, src));
      }
      break;
    default:
      LOG_FATAL("unexpected load result type");
      break;
  }
}

void x64_backend_mov_value(struct x64_backend *backend, const Xbyak::Reg &dst,
                           const struct ir_value *v) {
  auto &e = *backend->codegen;

  if (ir_is_constant(v)) {
    switch (v->type) {
      case VALUE_I8:
        e.mov(dst.cvt8(), v->i8);
        break;
      case VALUE_I16:
        e.mov(dst.cvt16(), v->i16);
        break;
      case VALUE_I32:
        e.mov(dst.cvt32(), v->i32);
        break;
      case VALUE_I64:
        e.mov(dst.cvt64(), v->i64);
        break;
      default:
        LOG_FATAL("unexpected value type");
        break;
    }
    return;
  }

  switch (v->type) {
    case VALUE_I8:
      e.mov(dst.cvt8(), x64_backend_reg(backend, v));
      break;
    case VALUE_I16:
      e.mov(dst.cvt16(), x64_backend_reg(backend, v));
      break;
    case VALUE_I32:
      e.mov(dst.cvt32(), x64_backend_reg(backend, v));
      break;
    case VALUE_I64:
      e.mov(dst, x64_backend_reg(backend, v));
      break;
    default:
      LOG_FATAL("unexpected value type");
      break;
  }
}

const Xbyak::Address x64_backend_xmm_constant(struct x64_backend *backend,
                                              enum xmm_constant c) {
  auto &e = *backend->codegen;

  return e.ptr[e.rip + backend->xmm_const[c]];
}

void x64_backend_block_label(char *name, size_t size, struct ir_block *block) {
  snprintf(name, size, ".%p", block);
}

static void x64_backend_emit_thunks(struct x64_backend *backend) {
  auto &e = *backend->codegen;

  {
    for (int i = 0; i < 16; i++) {
      Xbyak::Reg64 dst(i);

      e.align(32);

      backend->load_thunk[i] = e.getCurr<void (*)()>();

      /* save caller-saved registers that our code uses and ensure stack is
         16-byte aligned */
      int save_mask = JIT_ALLOCATE | JIT_CALLER_SAVE;
      int offset = x64_backend_push_regs(backend, save_mask);
      offset = ALIGN_UP(offset + X64_STACK_SHADOW_SPACE + 8, 16) - 8;
      e.sub(e.rsp, offset);

      /* call the mmio handler */
      e.call(e.rax);

      /* restore caller-saved registers */
      e.add(e.rsp, offset);
      x64_backend_pop_regs(backend, save_mask);

      /* save mmio handler result */
      e.mov(dst, e.rax);

      /* return to jit code */
      e.ret();
    }
  }

  {
    e.align(32);

    backend->store_thunk = e.getCurr<void (*)()>();
    /* save caller-saved registers that our code uses and ensure stack is
       16-byte aligned */
    int save_mask = JIT_ALLOCATE | JIT_CALLER_SAVE;
    int offset = x64_backend_push_regs(backend, save_mask);
    offset = ALIGN_UP(offset + X64_STACK_SHADOW_SPACE + 8, 16) - 8;
    e.sub(e.rsp, offset);

    /* call the mmio handler */
    e.call(e.rax);

    /* restore caller-saved registers */
    e.add(e.rsp, offset);
    x64_backend_pop_regs(backend, save_mask);

    /* return to jit code */
    e.ret();
  }
}

static void x64_backend_emit_constants(struct x64_backend *backend) {
  auto &e = *backend->codegen;

  /* xmm constants. SSE / AVX provides no support for loading a constant into an
     xmm register, so instead frequently used constants are emitted to the code
     buffer and used as memory operands */

  e.align(32);
  e.L(backend->xmm_const[XMM_CONST_PS_ABS_MASK]);
  e.dq(INT64_C(0x7fffffff7fffffff));
  e.dq(INT64_C(0x7fffffff7fffffff));

  e.align(32);
  e.L(backend->xmm_const[XMM_CONST_PD_ABS_MASK]);
  e.dq(INT64_C(0x7fffffffffffffff));
  e.dq(INT64_C(0x7fffffffffffffff));

  e.align(32);
  e.L(backend->xmm_const[XMM_CONST_PS_SIGN_MASK]);
  e.dq(INT64_C(0x8000000080000000));
  e.dq(INT64_C(0x8000000080000000));

  e.align(32);
  e.L(backend->xmm_const[XMM_CONST_PD_SIGN_MASK]);
  e.dq(INT64_C(0x8000000000000000));
  e.dq(INT64_C(0x8000000000000000));

  e.align(32);
  e.L(backend->xmm_const[XMM_CONST_PD_MIN_INT32]);
  double dbl_min_i32 = INT32_MIN;
  e.dq(*(uint64_t *)&dbl_min_i32);
  e.dq(*(uint64_t *)&dbl_min_i32);

  e.align(32);
  e.L(backend->xmm_const[XMM_CONST_PD_MAX_INT32]);
  double dbl_max_i32 = INT32_MAX;
  e.dq(*(uint64_t *)&dbl_max_i32);
  e.dq(*(uint64_t *)&dbl_max_i32);
}

static int x64_backend_handle_exception(struct jit_backend *base,
                                        struct exception_state *ex) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  struct jit_guest *guest = backend->base.guest;

  const uint8_t *data = (const uint8_t *)ex->thread_state.rip;

  /* figure out the guest address that was being accessed */
  const uint8_t *fault_addr = (const uint8_t *)ex->fault_addr;
  const uint8_t *protected_start = (const uint8_t *)ex->thread_state.r15;
  uint32_t guest_addr = (uint32_t)(fault_addr - protected_start);

  /* ensure it was an mmio address that caused the exception */
  uint8_t *ptr;
  guest->lookup(guest->mem, guest_addr, NULL, &ptr, NULL, NULL);

  if (ptr) {
    return 0;
  }

  /* it's assumed a mov has triggered the exception */
  struct x64_mov mov;
  if (!x64_decode_mov(data, &mov)) {
    return 0;
  }

  /* instead of handling the mmio callback from inside of the exception
     handler, force rip to the beginning of a thunk which will invoke the
     callback once the exception handler has exited. this frees the callbacks
     from any restrictions imposed by an exception handler, and also prevents
     a possible recursive exception

     push the return address (the next instruction after the current mov) to
     the stack. each thunk will be responsible for pushing / popping caller-
     saved registers */
  ex->thread_state.rsp -= 8;
  *(uint64_t *)(ex->thread_state.rsp) = ex->thread_state.rip + mov.length;
  CHECK(ex->thread_state.rsp % 16 == 8);

  if (mov.is_load) {
    /* prep argument registers (memory object, guest_addr) for read function */
    ex->thread_state.r[x64_arg0_idx] = (uint64_t)guest->mem;
    ex->thread_state.r[x64_arg1_idx] = (uint64_t)guest_addr;

    /* prep function call address for thunk */
    switch (mov.operand_size) {
      case 1:
        ex->thread_state.rax = (uint64_t)guest->r8;
        break;
      case 2:
        ex->thread_state.rax = (uint64_t)guest->r16;
        break;
      case 4:
        ex->thread_state.rax = (uint64_t)guest->r32;
        break;
      case 8:
        ex->thread_state.rax = (uint64_t)guest->r64;
        break;
    }

    /* resume execution in the thunk once the exception handler exits */
    ex->thread_state.rip = (uint64_t)backend->load_thunk[mov.reg];
  } else {
    /* prep argument registers (memory object, guest_addr, value) for write
       function */
    ex->thread_state.r[x64_arg0_idx] = (uint64_t)guest->mem;
    ex->thread_state.r[x64_arg1_idx] = (uint64_t)guest_addr;
    ex->thread_state.r[x64_arg2_idx] =
        mov.has_imm ? mov.imm : ex->thread_state.r[mov.reg];

    /* prep function call address for thunk */
    switch (mov.operand_size) {
      case 1:
        ex->thread_state.rax = (uint64_t)guest->w8;
        break;
      case 2:
        ex->thread_state.rax = (uint64_t)guest->w16;
        break;
      case 4:
        ex->thread_state.rax = (uint64_t)guest->w32;
        break;
      case 8:
        ex->thread_state.rax = (uint64_t)guest->w64;
        break;
    }

    /* resume execution in the thunk once the exception handler exits */
    ex->thread_state.rip = (uint64_t)backend->store_thunk;
  }

  return 1;
}

static void x64_backend_dump_code(struct jit_backend *base, const uint8_t *addr,
                                  int size, FILE *output) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  cs_insn *insns;
  size_t count = cs_disasm(backend->capstone_handle, addr, size, 0, 0, &insns);

  fprintf(output, "#==--------------------------------------------------==#\n");
  fprintf(output, "# x64\n");
  fprintf(output, "#==--------------------------------------------------==#\n");

  const int max_mnemonic_width = 8;

  for (size_t i = 0; i < count; i++) {
    cs_insn &insn = insns[i];
    fprintf(output, "# 0x%08" PRIx64 "  %-*s %s\n", insn.address,
            max_mnemonic_width, insn.mnemonic, insn.op_str);
  }

  cs_free(insns, count);
}

void x64_backend_emit_branch(struct x64_backend *backend, struct ir *ir,
                             const ir_value *target) {
  struct jit_guest *guest = backend->base.guest;
  auto &e = *backend->codegen;

  char block_label[128];
  int dispatch_type = 0;

  /* update guest pc */
  if (target) {
    if (ir_is_constant(target)) {
      if (target->type == VALUE_BLOCK) {
        x64_backend_block_label(block_label, sizeof(block_label), target->blk);

        struct ir_value *addr = ir_get_meta(ir, target->blk, IR_META_ADDR);
        e.mov(e.dword[guestctx + guest->offset_pc], addr->i32);
        dispatch_type = 0;
      } else {
        uint32_t addr = target->i32;
        e.mov(e.dword[guestctx + guest->offset_pc], addr);
        dispatch_type = 1;
      }
    } else {
      Xbyak::Reg addr = x64_backend_reg(backend, target);
      e.mov(e.dword[guestctx + guest->offset_pc], addr);
      dispatch_type = 2;
    }
  } else {
    dispatch_type = 2;
  }

  /* jump directly to the block / to dispatch */
  switch (dispatch_type) {
    case 0:
      e.jmp(block_label, Xbyak::CodeGenerator::T_NEAR);
      break;
    case 1:
      e.call(backend->dispatch_static);
      break;
    case 2:
      e.jmp(backend->dispatch_dynamic);
      break;
  }
}

static void x64_backend_emit_epilog(struct x64_backend *backend, struct ir *ir,
                                    struct ir_block *block) {
  auto &e = *backend->codegen;

  /* if the block didn't branch to another address, return to dispatch */
  struct ir_instr *last_instr =
      list_last_entry(&block->instrs, struct ir_instr, it);

  if (last_instr->op != OP_BRANCH && last_instr->op != OP_BRANCH_COND) {
    x64_backend_emit_branch(backend, ir, NULL);
  }
}

static void x64_backend_emit_prolog(struct x64_backend *backend, struct ir *ir,
                                    struct ir_block *block) {
  struct jit_guest *guest = backend->base.guest;

  auto &e = *backend->codegen;

  /* count number of instrs / cycles in the block */
  int num_instrs = 0;
  int num_cycles = 0;

  list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
    if (instr->op == OP_SOURCE_INFO) {
      num_instrs += 1;
      num_cycles += instr->arg[1]->i32;
    }
  }

  /* yield control once remaining cycles are executed */
  e.mov(e.eax, e.dword[guestctx + guest->offset_cycles]);
  e.test(e.eax, e.eax);
  e.js(backend->dispatch_exit);

  /* yield control to any pending interrupts */
  e.mov(e.rax, e.qword[guestctx + guest->offset_interrupts]);
  e.test(e.rax, e.rax);
  e.jnz(backend->dispatch_interrupt);

  /* update debug run counts */
  e.sub(e.dword[guestctx + guest->offset_cycles], num_cycles);
  e.add(e.dword[guestctx + guest->offset_instrs], num_instrs);
}

static void x64_backend_emit(struct x64_backend *backend, struct ir *ir,
                             jit_emit_cb emit_cb, void *emit_data) {
  auto &e = *backend->codegen;

  CHECK_LT(ir->locals_size, X64_STACK_SIZE);

  e.inLocalLabel();

  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    int first = 1;
    uint8_t *block_addr = e.getCurr<uint8_t *>();

    /* label each block for local branches */
    char block_label[128];
    x64_backend_block_label(block_label, sizeof(block_label), block);
    e.L(block_label);

    x64_backend_emit_prolog(backend, ir, block);

    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      /* call emit callback for each guest block / instruction enabling users
         to map each to their corresponding host address */
      if (emit_cb && instr->op == OP_SOURCE_INFO) {
        uint32_t guest_addr = instr->arg[0]->i32;

        if (first) {
          emit_cb(emit_data, JIT_EMIT_BLOCK, guest_addr, block_addr);
          first = 0;
        }

        uint8_t *instr_addr = e.getCurr<uint8_t *>();
        emit_cb(emit_data, JIT_EMIT_INSTR, guest_addr, instr_addr);
      }

      struct jit_emitter *emitter = &x64_emitters[instr->op];
      x64_emit_cb emit = (x64_emit_cb)emitter->func;
      CHECK_NOTNULL(emit);
      emit(backend, e, ir, instr);
    }

    x64_backend_emit_epilog(backend, ir, block);
  }

  e.outLocalLabel();
}

static int x64_backend_assemble_code(struct jit_backend *base, struct ir *ir,
                                     uint8_t **addr, int *size,
                                     jit_emit_cb emit_cb, void *emit_data) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  auto &e = *backend->codegen;

  int res = 1;
  uint8_t *code = e.getCurr<uint8_t *>();

  /* try to generate the x64 code. if the code buffer overflows let the backend
     know so it can reset the cache and try again */
  try {
    x64_backend_emit(backend, ir, emit_cb, emit_data);
  } catch (const Xbyak::Error &e) {
    if (e != Xbyak::ERR_CODE_IS_TOO_BIG) {
      LOG_FATAL("x64 codegen failure, %s", e.what());
    }
    res = 0;
  }

  /* return code address */
  *addr = code;
  *size = (int)(e.getCurr<uint8_t *>() - code);

  return res;
}

static void x64_backend_reset(struct jit_backend *base) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  /* avoid reemitting thunks by just resetting the size to a safe spot after
     the thunks */
  backend->codegen->setSize(X64_THUNK_SIZE);
}

static void x64_backend_destroy(struct jit_backend *base) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  cs_close(&backend->capstone_handle);

  delete backend->codegen;

  x64_dispatch_shutdown(backend);

  free(backend);
}

struct jit_backend *x64_backend_create(struct jit_guest *guest, void *code,
                                       int code_size) {
  struct x64_backend *backend =
      (struct x64_backend *)calloc(1, sizeof(struct x64_backend));
  Xbyak::util::Cpu cpu;

  backend->base.guest = guest;
  backend->base.destroy = &x64_backend_destroy;

  /* compile interface */
  backend->base.registers = x64_registers;
  backend->base.num_registers = ARRAY_SIZE(x64_registers);
  backend->base.emitters = x64_emitters;
  backend->base.num_emitters = ARRAY_SIZE(x64_emitters);
  backend->base.reset = &x64_backend_reset;
  backend->base.assemble_code = &x64_backend_assemble_code;
  backend->base.dump_code = &x64_backend_dump_code;
  backend->base.handle_exception = &x64_backend_handle_exception;

  /* dispatch interface */
  backend->base.run_code = &x64_dispatch_run_code;
  backend->base.lookup_code = &x64_dispatch_lookup_code;
  backend->base.cache_code = &x64_dispatch_cache_code;
  backend->base.invalidate_code = &x64_dispatch_invalidate_code;
  backend->base.patch_edge = &x64_dispatch_patch_edge;
  backend->base.restore_edge = &x64_dispatch_restore_edge;

  /* setup codegen buffer */
  int r = protect_pages(code, code_size, ACC_READWRITEEXEC);
  CHECK(r);

  int have_avx2 = cpu.has(Xbyak::util::Cpu::tAVX2);
  int have_sse2 = cpu.has(Xbyak::util::Cpu::tSSE2);
  CHECK(have_avx2 || have_sse2, "CPU must support either AVX2 or SSE2");

  backend->codegen = new Xbyak::CodeGenerator(code_size, code);
  backend->use_avx = have_avx2;

  /* create disassembler */
  int res = cs_open(CS_ARCH_X86, CS_MODE_64, &backend->capstone_handle);
  CHECK_EQ(res, CS_ERR_OK);

  /* emit initial thunks */
  x64_dispatch_init(backend);
  x64_dispatch_emit_thunks(backend);
  x64_backend_emit_thunks(backend);
  x64_backend_emit_constants(backend);
  CHECK_LT(backend->codegen->getSize(), X64_THUNK_SIZE);

  return &backend->base;
}
