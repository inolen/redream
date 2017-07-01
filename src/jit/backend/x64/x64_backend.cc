#include "jit/backend/x64/x64_local.h"

extern "C" {
#include "core/exception_handler.h"
#include "core/memory.h"
#include "core/profiler.h"
#include "jit/backend/jit_backend.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/backend/x64/x64_disassembler.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
}

/*
 * x64 register layout
 */

/* %rax %eax %ax %al      <-- both: temporary
   %rcx %ecx %cx %cl      <-- both: argument
   %rdx %edx %dx %dl      <-- both: argument
   %rbx %ebx %bx %bl      <-- both: available (callee saved)
   %rsp %esp %sp %spl     <-- both: reserved
   %rbp %ebp %bp %bpl     <-- both: available (callee saved)
   %rsi %esi %si %sil     <-- msvc: available (callee saved), amd64: argument
   %rdi %edi %di %dil     <-- msvc: available (callee saved), amd64: argument
   %r8 %r8d %r8w %r8b     <-- both: argument
   %r9 %r9d %r9w %r9b     <-- both: argument
   %r10 %r10d %r10w %r10b <-- both: available (not callee saved)
   %r11 %r11d %r11w %r11b <-- both: available (not callee saved)
   %r12 %r12d %r12w %r12b <-- both: available (callee saved)
   %r13 %r13d %r13w %r13b <-- both: available (callee saved)
   %r14 %r14d %r14w %r14b <-- both: available (callee saved)
   %r15 %r15d %r15w %r15b <-- both: available (callee saved)

   msvc calling convention uses rcx, rdx, r8, r9 for arguments
   amd64 calling convention uses rdi, rsi, rdx, rcx, r8, r9 for arguments
   both use the same xmm registers for floating point arguments
   our largest function call uses only 3 arguments
   msvc is left with rax, rsi, rdi, r10 and r11
   amd64 is left with rax, r8, r9, r10 and r11

   rax is used as a scratch register
   r10, r11, xmm1 are used for constant not eliminated by const propagation
   r14, r15 are reserved for the context and memory pointers */

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
const int x64_tmp0_idx = Xbyak::Operand::R10;
const int x64_tmp1_idx = Xbyak::Operand::R11;

const Xbyak::Reg64 arg0(x64_arg0_idx);
const Xbyak::Reg64 arg1(x64_arg1_idx);
const Xbyak::Reg64 arg2(x64_arg2_idx);
const Xbyak::Reg64 arg3(x64_arg3_idx);
const Xbyak::Reg64 tmp0(x64_tmp0_idx);
const Xbyak::Reg64 tmp1(x64_tmp1_idx);
const Xbyak::Reg64 guestctx(Xbyak::Operand::R14);
const Xbyak::Reg64 guestmem(Xbyak::Operand::R15);

const struct jit_register x64_registers[] = {
    {"rbx",   VALUE_INT_MASK,    JIT_CALLEE_SAVED, (const void *)&Xbyak::util::rbx},
    {"rbp",   VALUE_INT_MASK,    JIT_CALLEE_SAVED, (const void *)&Xbyak::util::rbp},
#if PLATFORM_WINDOWS
    {"rsi",   VALUE_INT_MASK,    JIT_CALLER_SAVED, (const void *)&Xbyak::util::rsi},
    {"rdi",   VALUE_INT_MASK,    JIT_CALLER_SAVED, (const void *)&Xbyak::util::rdi},
#else
    {"r8",    VALUE_INT_MASK,    JIT_CALLER_SAVED, (const void *)&Xbyak::util::r8},
    {"r9",    VALUE_INT_MASK,    JIT_CALLER_SAVED, (const void *)&Xbyak::util::r9},
#endif
    {"r12",   VALUE_INT_MASK,    JIT_CALLEE_SAVED, (const void *)&Xbyak::util::r12},
    {"r13",   VALUE_INT_MASK,    JIT_CALLEE_SAVED, (const void *)&Xbyak::util::r13},
    {"xmm6",  VALUE_FLOAT_MASK,  JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm6},
    {"xmm7",  VALUE_FLOAT_MASK,  JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm7},
    {"xmm8",  VALUE_FLOAT_MASK,  JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm8},
    {"xmm9",  VALUE_FLOAT_MASK,  JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm9},
    {"xmm10", VALUE_FLOAT_MASK,  JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm10},
    {"xmm11", VALUE_VECTOR_MASK, JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm11},
    {"xmm12", VALUE_VECTOR_MASK, JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm12},
    {"xmm13", VALUE_VECTOR_MASK, JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm13},
    {"xmm14", VALUE_VECTOR_MASK, JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm14},
    {"xmm15", VALUE_VECTOR_MASK, JIT_CALLEE_SAVED, (const void *)&Xbyak::util::xmm15}
};

const int x64_num_registers = array_size(x64_registers);
/* clang-format on */

const Xbyak::Reg x64_backend_reg(struct x64_backend *backend,
                                 const struct ir_value *v) {
  auto &e = *backend->codegen;

  /* if the value is a local or constant, copy it to a tempory register, else
     return the register allocated for it */
  if (ir_is_constant(v)) {
    CHECK_LT(backend->num_temps, 2);

    Xbyak::Reg tmp = backend->num_temps++ ? tmp1 : tmp0;

    switch (v->type) {
      case VALUE_I8:
        tmp = tmp.cvt8();
        break;
      case VALUE_I16:
        tmp = tmp.cvt16();
        break;
      case VALUE_I32:
        tmp = tmp.cvt32();
        break;
      case VALUE_I64:
        /* no conversion needed */
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }

    /* copy value to the temporary register */
    e.mov(tmp, ir_zext_constant(v));

    return tmp;
  }

  int i = v->reg;
  CHECK_NE(i, NO_REGISTER);

  const Xbyak::Reg &reg =
      *reinterpret_cast<const Xbyak::Reg *>(x64_registers[i].data);
  CHECK(reg.isREG());

  switch (v->type) {
    case VALUE_I8:
      return reg.cvt8();
    case VALUE_I16:
      return reg.cvt16();
    case VALUE_I32:
      return reg.cvt32();
    case VALUE_I64:
      return reg;
    default:
      LOG_FATAL("Unexpected value type");
      break;
  }
}

const Xbyak::Xmm x64_backend_xmm(struct x64_backend *backend,
                                 const struct ir_value *v) {
  auto &e = *backend->codegen;

  /* if the value isn't allocated a XMM register copy it to a temporary XMM,
     register, else return the XMM register allocated for it */
  if (ir_is_constant(v)) {
    /* copy value to the temporary register */
    if (v->type == VALUE_F32) {
      float val = v->f32;
      e.mov(e.eax, *(int32_t *)&val);
      if (X64_USE_AVX) {
        e.vmovd(e.xmm1, e.eax);
      } else {
        e.movd(e.xmm1, e.eax);
      }
    } else {
      double val = v->f64;
      e.mov(e.rax, *(int64_t *)&val);
      if (X64_USE_AVX) {
        e.vmovq(e.xmm1, e.rax);
      } else {
        e.movq(e.xmm1, e.rax);
      }
    }
    return e.xmm1;
  }

  int i = v->reg;
  CHECK_NE(i, NO_REGISTER);

  const Xbyak::Xmm &xmm =
      *reinterpret_cast<const Xbyak::Xmm *>(x64_registers[i].data);
  CHECK(xmm.isXMM());
  return xmm;
}

void x64_backend_load_mem(struct x64_backend *backend,
                          const struct ir_value *dst,
                          const Xbyak::RegExp &srcExp) {
  auto &e = *backend->codegen;

  switch (dst->type) {
    case VALUE_I8:
      e.mov(x64_backend_reg(backend, dst), e.byte[srcExp]);
      break;
    case VALUE_I16:
      e.mov(x64_backend_reg(backend, dst), e.word[srcExp]);
      break;
    case VALUE_I32:
      e.mov(x64_backend_reg(backend, dst), e.dword[srcExp]);
      break;
    case VALUE_I64:
      e.mov(x64_backend_reg(backend, dst), e.qword[srcExp]);
      break;
    case VALUE_F32:
      if (X64_USE_AVX) {
        e.vmovss(x64_backend_xmm(backend, dst), e.dword[srcExp]);
      } else {
        e.movss(x64_backend_xmm(backend, dst), e.dword[srcExp]);
      }
      break;
    case VALUE_F64:
      if (X64_USE_AVX) {
        e.vmovsd(x64_backend_xmm(backend, dst), e.qword[srcExp]);
      } else {
        e.movsd(x64_backend_xmm(backend, dst), e.qword[srcExp]);
      }
      break;
    case VALUE_V128:
      if (X64_USE_AVX) {
        e.vmovups(x64_backend_xmm(backend, dst), e.ptr[srcExp]);
      } else {
        e.movups(x64_backend_xmm(backend, dst), e.ptr[srcExp]);
      }
      break;
    default:
      LOG_FATAL("Unexpected load result type");
      break;
  }
}

void x64_backend_store_mem(struct x64_backend *backend,
                           const Xbyak::RegExp &dstExp,
                           const struct ir_value *src) {
  auto &e = *backend->codegen;

  switch (src->type) {
    case VALUE_I8:
      e.mov(e.byte[dstExp], x64_backend_reg(backend, src));
      break;
    case VALUE_I16:
      e.mov(e.word[dstExp], x64_backend_reg(backend, src));
      break;
    case VALUE_I32:
      e.mov(e.dword[dstExp], x64_backend_reg(backend, src));
      break;
    case VALUE_I64:
      e.mov(e.qword[dstExp], x64_backend_reg(backend, src));
      break;
    case VALUE_F32:
      if (X64_USE_AVX) {
        e.vmovss(e.dword[dstExp], x64_backend_xmm(backend, src));
      } else {
        e.movss(e.dword[dstExp], x64_backend_xmm(backend, src));
      }
      break;
    case VALUE_F64:
      if (X64_USE_AVX) {
        e.vmovsd(e.qword[dstExp], x64_backend_xmm(backend, src));
      } else {
        e.movsd(e.qword[dstExp], x64_backend_xmm(backend, src));
      }
      break;
    case VALUE_V128:
      if (X64_USE_AVX) {
        e.vmovups(e.ptr[dstExp], x64_backend_xmm(backend, src));
      } else {
        e.movups(e.ptr[dstExp], x64_backend_xmm(backend, src));
      }
      break;
    default:
      LOG_FATAL("Unexpected load result type");
      break;
  }
}

void x64_backend_mov_value(struct x64_backend *backend, Xbyak::Reg dst,
                           const struct ir_value *v) {
  auto &e = *backend->codegen;

  const Xbyak::Reg src = x64_backend_reg(backend, v);

  switch (v->type) {
    case VALUE_I8:
      e.mov(dst.cvt8(), src);
      break;
    case VALUE_I16:
      e.mov(dst.cvt16(), src);
      break;
    case VALUE_I32:
      e.mov(dst.cvt32(), src);
      break;
    case VALUE_I64:
      e.mov(dst, src);
      break;
    default:
      LOG_FATAL("Unexpected value type");
      break;
  }
}

const Xbyak::Address x64_backend_xmm_constant(struct x64_backend *backend,
                                              enum xmm_constant c) {
  auto &e = *backend->codegen;

  return e.ptr[e.rip + backend->xmm_const[c]];
}

int x64_backend_can_encode_imm(const struct ir_value *v) {
  if (!ir_is_constant(v)) {
    return 0;
  }

  return v->type <= VALUE_I32;
}

static void x64_backend_block_label(char *name, size_t size,
                                    struct ir_block *block) {
  snprintf(name, size, ".%p", block);
}

static void x64_backend_label_name(char *name, size_t size,
                                   struct ir_value *v) {
  /* all ir labels are local labels */
  snprintf(name, size, ".%s", v->str);
}

static void x64_backend_emit_epilogue(struct x64_backend *backend,
                                      struct jit_block *block) {
  auto &e = *backend->codegen;

  /* catch blocks that haven't been terminated */
  e.db(0xcc);
}

static void x64_backend_emit_prologue(struct x64_backend *backend,
                                      struct jit_block *block) {
  struct jit *jit = backend->base.jit;
  struct jit_guest *guest = jit->guest;

  auto &e = *backend->codegen;

  /* yield control once remaining cycles are executed */
  e.mov(e.eax, e.dword[guestctx + guest->offset_cycles]);
  e.test(e.eax, e.eax);
  e.js(backend->dispatch_exit);

  /* handle pending interrupts */
  e.mov(e.rax, e.qword[guestctx + guest->offset_interrupts]);
  e.test(e.rax, e.rax);
  e.jnz(backend->dispatch_interrupt);

  /* update run counts */
  e.sub(e.dword[guestctx + guest->offset_cycles], block->num_cycles);
  e.add(e.dword[guestctx + guest->offset_instrs], block->num_instrs);
}

static void x64_backend_emit(struct x64_backend *backend,
                             struct jit_block *block, struct ir *ir) {
  auto &e = *backend->codegen;
  const uint8_t *code = backend->codegen->getCurr();

  CHECK_LT(ir->locals_size, X64_STACK_SIZE);

  e.inLocalLabel();

  x64_backend_emit_prologue(backend, block);

  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    char block_label[128];
    x64_backend_block_label(block_label, sizeof(block_label), block);

    e.L(block_label);

    int terminated = 0;

    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      struct jit_emitter *emitter = &x64_emitters[instr->op];
      x64_emit_cb emit = (x64_emit_cb)emitter->func;
      CHECK_NOTNULL(emit);

      /* reset temp count used by x64_backend_reg */
      backend->num_temps = 0;

      emit(backend, *backend->codegen, instr);

      terminated = (instr->op == OP_BRANCH);
    }

    /* if the block doesn't terminate in an unconditional branch, dispatch to
       the next pc, which has ideally been set by a non-branch operation such
       as a fallback handler */
    if (!terminated) {
      e.jmp(backend->dispatch_dynamic);
    }
  }

  x64_backend_emit_epilogue(backend, block);

  e.outLocalLabel();

  block->host_addr = (void *)code;
  block->host_size = (int)(backend->codegen->getCurr() - code);
}

static void x64_backend_emit_thunks(struct x64_backend *backend) {
  auto &e = *backend->codegen;

  {
    for (int i = 0; i < 16; i++) {
      Xbyak::Reg64 dst(i);

      e.align(32);

      backend->load_thunk[i] = e.getCurr<void (*)()>();

      /* call the mmio handler */
      e.call(e.rax);

      /* restore caller-saved registers */
      e.add(e.rsp, X64_STACK_SHADOW_SPACE + 8);
#if PLATFORM_WINDOWS
      e.pop(e.rdi);
      e.pop(e.rsi);
#else
      e.pop(e.r9);
      e.pop(e.r8);
#endif

      /* save mmio handler result */
      e.mov(dst, e.rax);

      /* return to jit code */
      e.ret();
    }
  }

  {
    e.align(32);

    backend->store_thunk = e.getCurr<void (*)()>();

    /* call the mmio handler */
    e.call(e.rax);

    /* restore caller-saved registers */
    e.add(e.rsp, X64_STACK_SHADOW_SPACE + 8);
#if PLATFORM_WINDOWS
    e.pop(e.rdi);
    e.pop(e.rsi);
#else
    e.pop(e.r9);
    e.pop(e.r8);
#endif

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
  e.L(backend->xmm_const[XMM_CONST_ABS_MASK_PS]);
  e.dq(INT64_C(0x7fffffff7fffffff));
  e.dq(INT64_C(0x7fffffff7fffffff));

  e.align(32);
  e.L(backend->xmm_const[XMM_CONST_ABS_MASK_PD]);
  e.dq(INT64_C(0x7fffffffffffffff));
  e.dq(INT64_C(0x7fffffffffffffff));

  e.align(32);
  e.L(backend->xmm_const[XMM_CONST_SIGN_MASK_PS]);
  e.dq(INT64_C(0x8000000080000000));
  e.dq(INT64_C(0x8000000080000000));

  e.align(32);
  e.L(backend->xmm_const[XMM_CONST_SIGN_MASK_PD]);
  e.dq(INT64_C(0x8000000000000000));
  e.dq(INT64_C(0x8000000000000000));
}

static int x64_backend_handle_exception(struct jit_backend *base,
                                        struct exception_state *ex) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  struct jit_guest *guest = backend->base.jit->guest;

  const uint8_t *data = reinterpret_cast<const uint8_t *>(ex->thread_state.rip);

  /* figure out the guest address that was being accessed */
  const uint8_t *fault_addr = reinterpret_cast<const uint8_t *>(ex->fault_addr);
  const uint8_t *protected_start =
      reinterpret_cast<const uint8_t *>(ex->thread_state.r15);
  uint32_t guest_addr = static_cast<uint32_t>(fault_addr - protected_start);

  /* ensure it was an mmio address that caused the exception */
  void *ptr;
  guest->lookup(guest->space, guest_addr, &ptr, NULL, NULL, NULL, NULL);

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

   push all of the caller saved registers used by the jit, as well as the
   return address (the next instruction after the current mov) to the stack.
   add an extra 8 bytes to keep the stack aligned */
#if PLATFORM_WINDOWS
  *(uint64_t *)(ex->thread_state.rsp - 24) = ex->thread_state.rdi;
  *(uint64_t *)(ex->thread_state.rsp - 16) = ex->thread_state.rsi;
#else
  *(uint64_t *)(ex->thread_state.rsp - 24) = ex->thread_state.r9;
  *(uint64_t *)(ex->thread_state.rsp - 16) = ex->thread_state.r8;
#endif
  *(uint64_t *)(ex->thread_state.rsp - 8) = ex->thread_state.rip + mov.length;
  ex->thread_state.rsp -= X64_STACK_SHADOW_SPACE + 24 + 8;
  CHECK(ex->thread_state.rsp % 16 == 0);

  if (mov.is_load) {
    /* prep argument registers (memory object, guest_addr) for read function */
    ex->thread_state.r[x64_arg0_idx] = reinterpret_cast<uint64_t>(guest->space);
    ex->thread_state.r[x64_arg1_idx] = static_cast<uint64_t>(guest_addr);

    /* prep function call address for thunk */
    switch (mov.operand_size) {
      case 1:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(guest->r8);
        break;
      case 2:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(guest->r16);
        break;
      case 4:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(guest->r32);
        break;
      case 8:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(guest->r64);
        break;
    }

    /* resume execution in the thunk once the exception handler exits */
    ex->thread_state.rip =
        reinterpret_cast<uint64_t>(backend->load_thunk[mov.reg]);
  } else {
    /* prep argument registers (memory object, guest_addr, value) for write
       function */
    ex->thread_state.r[x64_arg0_idx] = reinterpret_cast<uint64_t>(guest->space);
    ex->thread_state.r[x64_arg1_idx] = static_cast<uint64_t>(guest_addr);
    ex->thread_state.r[x64_arg2_idx] = ex->thread_state.r[mov.reg];

    /* prep function call address for thunk */
    switch (mov.operand_size) {
      case 1:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(guest->w8);
        break;
      case 2:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(guest->w16);
        break;
      case 4:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(guest->w32);
        break;
      case 8:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(guest->w64);
        break;
    }

    /* resume execution in the thunk once the exception handler exits */
    ex->thread_state.rip = reinterpret_cast<uint64_t>(backend->store_thunk);
  }

  return 1;
}

static void x64_backend_dump_code(struct jit_backend *base,
                                  const struct jit_block *block) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  const uint8_t *code = (const uint8_t *)block->host_addr;
  int size = block->host_size;

  cs_insn *insns;
  size_t count = cs_disasm(backend->capstone_handle, code, size, 0, 0, &insns);
  CHECK(count);

  for (size_t i = 0; i < count; i++) {
    cs_insn &insn = insns[i];
    LOG_INFO("0x%" PRIx64 ":\t%s\t\t%s", insn.address, insn.mnemonic,
             insn.op_str);
  }

  cs_free(insns, count);
}

static int x64_backend_assemble_code(struct jit_backend *base,
                                     struct jit_block *block, struct ir *ir) {
  PROF_ENTER("cpu", "x64_backend_assemble_code");

  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  int res = 1;

  /* try to generate the x64 code. if the code buffer overflows let the backend
     know so it can reset the cache and try again */
  try {
    x64_backend_emit(backend, block, ir);
  } catch (const Xbyak::Error &e) {
    if (e != Xbyak::ERR_CODE_IS_TOO_BIG) {
      LOG_FATAL("x64 codegen failure, %s", e.what());
    }
    res = 0;
  }

  PROF_LEAVE();

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

static void x64_backend_init(struct jit_backend *base) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  x64_dispatch_init(backend);

  /* emit thunks into a fixed amount of space to speed up resets */
  x64_dispatch_emit_thunks(backend);
  x64_backend_emit_thunks(backend);
  x64_backend_emit_constants(backend);
  CHECK_LT(backend->codegen->getSize(), X64_THUNK_SIZE);
}

struct jit_backend *x64_backend_create(void *code, int code_size) {
  struct x64_backend *backend = reinterpret_cast<struct x64_backend *>(
      calloc(1, sizeof(struct x64_backend)));
  Xbyak::util::Cpu cpu;

  int r = protect_pages(code, code_size, ACC_READWRITEEXEC);
  CHECK(r);

  backend->base.init = &x64_backend_init;
  backend->base.destroy = &x64_backend_destroy;

  /* compile interface */
  backend->base.registers = x64_registers;
  backend->base.num_registers = array_size(x64_registers);
  backend->base.emitters = x64_emitters;
  backend->base.num_emitters = array_size(x64_emitters);
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

  backend->codegen = new Xbyak::CodeGenerator(code_size, code);
  backend->use_avx = cpu.has(Xbyak::util::Cpu::tAVX2);

  int res = cs_open(CS_ARCH_X86, CS_MODE_64, &backend->capstone_handle);
  CHECK_EQ(res, CS_ERR_OK);

  return &backend->base;
}
