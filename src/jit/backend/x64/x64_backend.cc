#include "jit/backend/x64/x64_local.h"

extern "C" {
#include "core/profiler.h"
#include "jit/backend/jit_backend.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/backend/x64/x64_disassembler.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "sys/exception_handler.h"
#include "sys/memory.h"
}

/*
 * x64 stack layout
 */

#if PLATFORM_WINDOWS
static const int STACK_SHADOW_SPACE = 32;
#else
static const int STACK_SHADOW_SPACE = 0;
#endif
static const int STACK_OFFSET_LOCALS = STACK_SHADOW_SPACE + 8;

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
   msvc is left with rax, rdi, rsi, r9-r11,
   amd64 is left with rax, rcx, r8-r11 available on amd64

   rax is used as a scratch register
   r10, r11, xmm1 are used for constant not eliminated by const propagation
   r14, r15 are reserved for the context and memory pointers */

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

const struct jit_register x64_registers[] = {
    {"rbx", VALUE_INT_MASK, (const void *)&Xbyak::util::rbx},
    {"rbp", VALUE_INT_MASK, (const void *)&Xbyak::util::rbp},
    {"r12", VALUE_INT_MASK, (const void *)&Xbyak::util::r12},
    {"r13", VALUE_INT_MASK, (const void *)&Xbyak::util::r13},
    /* {"r14", VALUE_INT_MASK, (const void *)&Xbyak::util::r14},
       {"r15", VALUE_INT_MASK, (const void *)&Xbyak::util::r15}, */
    {"xmm6", VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm6)},
    {"xmm7", VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm7)},
    {"xmm8", VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm8)},
    {"xmm9", VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm9)},
    {"xmm10", VALUE_FLOAT_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm10)},
    {"xmm11", VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm11)},
    {"xmm12", VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm12)},
    {"xmm13", VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm13)},
    {"xmm14", VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm14)},
    {"xmm15", VALUE_VECTOR_MASK,
     reinterpret_cast<const void *>(&Xbyak::util::xmm15)}};

const int x64_num_registers =
    sizeof(x64_registers) / sizeof(struct jit_register);

/*
 * x64 emitters for each ir op
 */
struct x64_backend;

typedef void (*x64_emit_cb)(struct x64_backend *, Xbyak::CodeGenerator &,
                            const struct ir_instr *);

static x64_emit_cb x64_backend_emitters[NUM_OPS];

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

#define USE_AVX backend->use_avx

static const Xbyak::Reg x64_backend_reg(struct x64_backend *backend,
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

static const Xbyak::Xmm x64_backend_xmm(struct x64_backend *backend,
                                        const struct ir_value *v) {
  auto &e = *backend->codegen;

  /* if the value isn't allocated a XMM register copy it to a temporary XMM,
     register, else return the XMM register allocated for it */
  if (ir_is_constant(v)) {
    /* copy value to the temporary register */
    if (v->type == VALUE_F32) {
      float val = v->f32;
      e.mov(e.eax, *(int32_t *)&val);
      if (USE_AVX) {
        e.vmovd(e.xmm1, e.eax);
      } else {
        e.movd(e.xmm1, e.eax);
      }
    } else {
      double val = v->f64;
      e.mov(e.rax, *(int64_t *)&val);
      if (USE_AVX) {
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

static void x64_backend_load_mem(struct x64_backend *backend,
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
      if (USE_AVX) {
        e.vmovss(x64_backend_xmm(backend, dst), e.dword[srcExp]);
      } else {
        e.movss(x64_backend_xmm(backend, dst), e.dword[srcExp]);
      }
      break;
    case VALUE_F64:
      if (USE_AVX) {
        e.vmovsd(x64_backend_xmm(backend, dst), e.qword[srcExp]);
      } else {
        e.movsd(x64_backend_xmm(backend, dst), e.qword[srcExp]);
      }
      break;
    case VALUE_V128:
      if (USE_AVX) {
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

static void x64_backend_store_mem(struct x64_backend *backend,
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
      if (USE_AVX) {
        e.vmovss(e.dword[dstExp], x64_backend_xmm(backend, src));
      } else {
        e.movss(e.dword[dstExp], x64_backend_xmm(backend, src));
      }
      break;
    case VALUE_F64:
      if (USE_AVX) {
        e.vmovsd(e.qword[dstExp], x64_backend_xmm(backend, src));
      } else {
        e.movsd(e.qword[dstExp], x64_backend_xmm(backend, src));
      }
      break;
    case VALUE_V128:
      if (USE_AVX) {
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

static void x64_backend_mov_value(struct x64_backend *backend, Xbyak::Reg dst,
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

static const Xbyak::Address x64_backend_xmm_constant(
    struct x64_backend *backend, enum xmm_constant c) {
  auto &e = *backend->codegen;

  return e.ptr[e.rip + backend->xmm_const[c]];
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

static int x64_backend_can_encode_imm(const struct ir_value *v) {
  if (!ir_is_constant(v)) {
    return 0;
  }

  return v->type <= VALUE_I32;
}

static void x64_backend_emit_epilogue(struct x64_backend *backend,
                                      struct jit_block *block) {}

static void x64_backend_emit_prologue(struct x64_backend *backend,
                                      struct jit_block *block) {
  struct jit *jit = backend->base.jit;
  struct jit_guest *guest = jit->guest;

  auto &e = *backend->codegen;

  /* yield control once remaining cycles are executed */
  e.mov(e.eax, e.dword[e.r14 + guest->offset_cycles]);
  e.test(e.eax, e.eax);
  e.js(backend->dispatch_exit);

  /* handle pending interrupts */
  e.mov(e.rax, e.qword[e.r14 + guest->offset_interrupts]);
  e.test(e.rax, e.rax);
  e.jnz(backend->dispatch_interrupt);

  /* update run counts */
  e.sub(e.dword[e.r14 + guest->offset_cycles], block->num_cycles);
  e.add(e.dword[e.r14 + guest->offset_instrs], block->num_instrs);
}

static void *x64_backend_emit(struct x64_backend *backend,
                              struct jit_block *block, struct ir *ir) {
  auto &e = *backend->codegen;
  void *code = (void *)backend->codegen->getCurr();

  CHECK_LT(ir->locals_size, X64_STACK_SIZE);

  e.inLocalLabel();

  x64_backend_emit_prologue(backend, block);

  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    char block_label[128];
    x64_backend_block_label(block_label, sizeof(block_label), block);

    e.L(block_label);

    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      x64_emit_cb emit = x64_backend_emitters[instr->op];
      CHECK_NOTNULL(emit);

      /* reset temp count used by x64_backend_get_register */
      backend->num_temps = 0;

      emit(backend, *backend->codegen, instr);
    }
  }

  x64_backend_emit_epilogue(backend, block);

  e.outLocalLabel();

  block->host_size =
      (int)((uint8_t *)backend->codegen->getCurr() - (uint8_t *)code);

  return code;
}

static void x64_backend_emit_thunks(struct x64_backend *backend) {
  auto &e = *backend->codegen;

  {
    for (int i = 0; i < 16; i++) {
      e.align(32);

      backend->load_thunk[i] = e.getCurr<void (*)()>();

      Xbyak::Reg64 dst(i);
      e.call(e.rax);
      e.mov(dst, e.rax);
      e.add(e.rsp, STACK_SHADOW_SPACE + 8);
      e.ret();
    }
  }

  {
    e.align(32);

    backend->store_thunk = e.getCurr<void (*)()>();

    e.call(e.rax);
    e.add(e.rsp, STACK_SHADOW_SPACE + 8);
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
                                        struct exception *ex) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  struct jit_guest *guest = backend->base.jit->guest;

  const uint8_t *data = reinterpret_cast<const uint8_t *>(ex->thread_state.rip);

  /* figure out the guest address that was being accessed */
  const uint8_t *fault_addr = reinterpret_cast<const uint8_t *>(ex->fault_addr);
  const uint8_t *protected_start =
      reinterpret_cast<const uint8_t *>(ex->thread_state.r15);
  uint32_t guest_addr = static_cast<uint32_t>(fault_addr - protected_start);

  /* ensure it was an MMIO address that caused the exception */
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

  /* instead of handling the MMIO callback from inside of the exception
     handler, force rip to the beginning of a thunk which will invoke the
     callback once the exception handler has exited. this frees the callbacks
     from any restrictions imposed by an exception handler, and also prevents
     a possible recursive exceptions

     push the return address (the next instruction after the current mov) to
     the stack. also, adjust the stack for the return address, with an extra
     8 bytes to keep it aligned */
  *(uintptr_t *)(ex->thread_state.rsp - 8) = ex->thread_state.rip + mov.length;
  ex->thread_state.rsp -= STACK_SHADOW_SPACE + 8 + 8;
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

static void x64_backend_dump_code(struct jit_backend *base, const uint8_t *code,
                                  int size) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

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
    block->host_addr = x64_backend_emit(backend, block, ir);
  } catch (const Xbyak::Error &e) {
    if (e != Xbyak::ERR_CODE_IS_TOO_BIG) {
      LOG_FATAL("X64 codegen failure, %s", e.what());
    }
    res = 0;
  }

  PROF_LEAVE();

  return res;
}

static void x64_backend_reset(struct jit_backend *base) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  /* avoid reemitting thunks and resetting dispatch cache by just resetting
     the size to a safe spot after the thunks */
  /*backend->codegen->reset();*/
  backend->codegen->setSize(X64_THUNK_SIZE);
}

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

      x64_backend_load_mem(backend, result, a.cvt64() + e.r15);
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

      x64_backend_store_mem(backend, a.cvt64() + e.r15, data);
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

  x64_backend_load_mem(backend, instr->result, a.cvt64() + e.r15);
}

EMITTER(STORE_FAST) {
  const Xbyak::Reg a = x64_backend_reg(backend, instr->arg[0]);

  x64_backend_store_mem(backend, a.cvt64() + e.r15, instr->arg[1]);
}

EMITTER(LOAD_CONTEXT) {
  int offset = instr->arg[0]->i32;

  x64_backend_load_mem(backend, instr->result, e.r14 + offset);
}

EMITTER(STORE_CONTEXT) {
  int offset = instr->arg[0]->i32;

  if (ir_is_constant(instr->arg[1])) {
    switch (instr->arg[1]->type) {
      case VALUE_I8:
        e.mov(e.byte[e.r14 + offset], instr->arg[1]->i8);
        break;
      case VALUE_I16:
        e.mov(e.word[e.r14 + offset], instr->arg[1]->i16);
        break;
      case VALUE_I32:
      case VALUE_F32:
        e.mov(e.dword[e.r14 + offset], instr->arg[1]->i32);
        break;
      case VALUE_I64:
      case VALUE_F64:
        e.mov(e.qword[e.r14 + offset], instr->arg[1]->i64);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    x64_backend_store_mem(backend, e.r14 + offset, instr->arg[1]);
  }
}

EMITTER(LOAD_LOCAL) {
  int offset = STACK_OFFSET_LOCALS + instr->arg[0]->i32;

  x64_backend_load_mem(backend, instr->result, e.rsp + offset);
}

EMITTER(STORE_LOCAL) {
  int offset = STACK_OFFSET_LOCALS + instr->arg[0]->i32;

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
    if (USE_AVX) {
      e.vaddss(result, a, b);
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.addss(result, b);
    }
  } else {
    if (USE_AVX) {
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
    if (USE_AVX) {
      e.vsubss(result, a, b);
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.subss(result, b);
    }
  } else {
    if (USE_AVX) {
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
    if (USE_AVX) {
      e.vmulss(result, a, b);
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.mulss(result, b);
    }
  } else {
    if (USE_AVX) {
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
    if (USE_AVX) {
      e.vdivss(result, a, b);
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.divss(result, b);
    }
  } else {
    if (USE_AVX) {
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
    if (USE_AVX) {
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
    if (USE_AVX) {
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
    if (USE_AVX) {
      e.vandps(result, a,
               x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PS));
    } else {
      if (result != a) {
        e.movss(result, a);
      }
      e.andps(result, x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PS));
    }
  } else {
    if (USE_AVX) {
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
    if (USE_AVX) {
      e.vsqrtss(result, a);
    } else {
      e.sqrtss(result, a);
    }
  } else {
    if (USE_AVX) {
      e.vsqrtsd(result, a);
    } else {
      e.sqrtsd(result, a);
    }
  }
}

EMITTER(VBROADCAST) {
  const Xbyak::Xmm result = x64_backend_xmm(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm(backend, instr->arg[0]);

  if (USE_AVX) {
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

  if (USE_AVX) {
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

  if (USE_AVX) {
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

  if (USE_AVX) {
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
    e.mov(e.dword[e.r14 + guest->offset_pc], branch_addr);
    e.call(backend->dispatch_static);
  } else {
    const Xbyak::Reg branch_addr = x64_backend_reg(backend, instr->arg[0]);
    e.mov(e.dword[e.r14 + guest->offset_pc], branch_addr);
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
    e.mov(e.dword[e.r14 + guest->offset_pc], branch_addr);
    e.call(backend->dispatch_static);
  } else {
    const Xbyak::Reg branch_addr = x64_backend_reg(backend, instr->arg[0]);
    e.mov(e.dword[e.r14 + guest->offset_pc], branch_addr);
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
    e.mov(e.dword[e.r14 + guest->offset_pc], branch_addr);
    e.call(backend->dispatch_static);
  } else {
    const Xbyak::Reg branch_addr = x64_backend_reg(backend, instr->arg[0]);
    e.mov(e.dword[e.r14 + guest->offset_pc], branch_addr);
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

EMITTER(FLUSH_CONTEXT) {}

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

  /* emit thunks into a fixed amount of space to speed up cache resets */
  x64_dispatch_emit_thunks(backend);
  x64_backend_emit_thunks(backend);
  x64_backend_emit_constants(backend);
  CHECK_LT(backend->codegen->getSize(), X64_THUNK_SIZE);
  x64_backend_reset(base);
}

struct jit_backend *x64_backend_create(void *code, int code_size) {
  struct x64_backend *backend = reinterpret_cast<struct x64_backend *>(
      calloc(1, sizeof(struct x64_backend)));
  Xbyak::util::Cpu cpu;

  CHECK(Xbyak::CodeArray::protect(code, code_size, true));

  backend->base.init = &x64_backend_init;
  backend->base.destroy = &x64_backend_destroy;

  /* compile interface */
  backend->base.registers = x64_registers;
  backend->base.num_registers = array_size(x64_registers);
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
