#include <capstone.h>
#include <inttypes.h>

#define XBYAK_NO_OP_NAMES
#include <xbyak/xbyak.h>

extern "C" {
#include "core/profiler.h"
#include "jit/backend/backend.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/backend/x64/x64_disassembler.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "sys/exception_handler.h"
#include "sys/memory.h"
}

//
// x64 stack layout
//

#if PLATFORM_WINDOWS
static const int STACK_SHADOW_SPACE = 32;
#else
static const int STACK_SHADOW_SPACE = 0;
#endif
static const int STACK_OFFSET_LOCALS = STACK_SHADOW_SPACE;
static const int STACK_SIZE = STACK_OFFSET_LOCALS;

//
// x64 register layout
//

// %rax %eax %ax %al      <-- both: temporary
// %rcx %ecx %cx %cl      <-- both: argument
// %rdx %edx %dx %dl      <-- both: argument
// %rbx %ebx %bx %bl      <-- both: available (callee saved)
// %rsp %esp %sp %spl     <-- both: reserved
// %rbp %ebp %bp %bpl     <-- both: available (callee saved)
// %rsi %esi %si %sil     <-- msvc: available (callee saved), amd64: argument
// %rdi %edi %di %dil     <-- msvc: available (callee saved), amd64: argument
// %r8 %r8d %r8w %r8b     <-- both: argument
// %r9 %r9d %r9w %r9b     <-- both: argument
// %r10 %r10d %r10w %r10b <-- both: available (not callee saved)
// %r11 %r11d %r11w %r11b <-- both: available (not callee saved)
// %r12 %r12d %r12w %r12b <-- both: available (callee saved)
// %r13 %r13d %r13w %r13b <-- both: available (callee saved)
// %r14 %r14d %r14w %r14b <-- both: available (callee saved)
// %r15 %r15d %r15w %r15b <-- both: available (callee saved)

// msvc calling convention uses rcx, rdx, r8, r9 for arguments
// amd64 calling convention uses rdi, rsi, rdx, rcx, r8, r9 for arguments
// both use the same xmm registers for floating point arguments
// our largest function call uses only 3 arguments
// msvc is left with rax, rdi, rsi, r9-r11,
// amd64 is left with rax, rcx, r8-r11 available on amd64

// rax is used as a scratch register
// r10, r11, xmm1 are used for constant not eliminated by const propagation
// r14, r15 are reserved for the context and memory pointers

#if PLATFORM_WINDOWS
const int x64_arg0_idx = Xbyak::Operand::RCX;
const int x64_arg1_idx = Xbyak::Operand::RDX;
const int x64_arg2_idx = Xbyak::Operand::R8;
#else
const int x64_arg0_idx = Xbyak::Operand::RDI;
const int x64_arg1_idx = Xbyak::Operand::RSI;
const int x64_arg2_idx = Xbyak::Operand::RDX;
#endif
const int x64_tmp0_idx = Xbyak::Operand::R10;
const int x64_tmp1_idx = Xbyak::Operand::R11;

const Xbyak::Reg64 arg0(x64_arg0_idx);
const Xbyak::Reg64 arg1(x64_arg1_idx);
const Xbyak::Reg64 arg2(x64_arg2_idx);
const Xbyak::Reg64 tmp0(x64_tmp0_idx);
const Xbyak::Reg64 tmp1(x64_tmp1_idx);

const struct jit_register x64_registers[] = {
    {"rbx", VALUE_INT_MASK, reinterpret_cast<const void *>(&Xbyak::util::rbx)},
    {"rbp", VALUE_INT_MASK, reinterpret_cast<const void *>(&Xbyak::util::rbp)},
    {"r12", VALUE_INT_MASK, reinterpret_cast<const void *>(&Xbyak::util::r12)},
    {"r13", VALUE_INT_MASK, reinterpret_cast<const void *>(&Xbyak::util::r13)},
    // {"r14", VALUE_INT_MASK,
    //  reinterpret_cast<const void *>(&Xbyak::util::r14)},
    // {"r15", VALUE_INT_MASK,
    //  reinterpret_cast<const void *>(&Xbyak::util::r15)},
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

//
// x64 code buffer. this will break down if running two instances of the x64
// backend, but it's extremely useful when profiling to group JITd blocks of
// code with an actual symbol name
//
static const size_t x64_code_size = 1024 * 1024 * 32;
static uint8_t x64_code[x64_code_size];

//
// x64 emitters for each ir op
//
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

//
// xmm constants. SSE / AVX provides no support for loading a constant into an
// xmm register, so instead frequently used constants are emitted to the code
// buffer and used as memory operands
//
enum xmm_constant {
  XMM_CONST_ABS_MASK_PS,
  XMM_CONST_ABS_MASK_PD,
  XMM_CONST_SIGN_MASK_PS,
  XMM_CONST_SIGN_MASK_PD,
  NUM_XMM_CONST,
};

struct x64_backend {
  struct jit_backend base;

  const struct jit_guest *guest;

  Xbyak::CodeGenerator *codegen;
  csh capstone_handle;
  Xbyak::Label xmm_const[NUM_XMM_CONST];
  void (*load_thunk[16])();
  void (*store_thunk)();
  bool modified[x64_num_registers];
  int num_temps;
};

const Xbyak::Reg x64_backend_register(struct x64_backend *backend,
                                      const struct ir_value *v) {
  auto &e = *backend->codegen;

  // if the value is a local or constant, copy it to a tempory register, else
  // return the register allocated for it
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
        // no conversion needed
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }

    // copy value to the temporary register
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

const Xbyak::Xmm x64_backend_xmm_register(struct x64_backend *backend,
                                          const struct ir_value *v) {
  auto &e = *backend->codegen;

  // if the value isn't allocated a XMM register copy it to a temporary XMM,
  // register, else return the XMM register allocated for it
  if (ir_is_constant(v)) {
    // copy value to the temporary register
    if (v->type == VALUE_F32) {
      float val = v->f32;
      e.mov(e.eax, *(int32_t *)&val);
      e.vmovd(e.xmm1, e.eax);
    } else {
      double val = v->f64;
      e.mov(e.rax, *(int64_t *)&val);
      e.vmovq(e.xmm1, e.rax);
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

const Xbyak::Address x64_backend_xmm_constant(struct x64_backend *backend,
                                              enum xmm_constant c) {
  auto &e = *backend->codegen;

  return e.ptr[e.rip + backend->xmm_const[c]];
}

static bool x64_backend_can_encode_imm(const struct ir_value *v) {
  if (!ir_is_constant(v)) {
    return false;
  }

  return v->type <= VALUE_I32;
}

static bool x64_backend_callee_saved(const Xbyak::Reg &reg) {
  if (reg.isXMM()) {
    return false;
  }

  static bool callee_saved[16] = {
    false,  // RAX
    false,  // RCX
    false,  // RDX
    true,   // RBX
    false,  // RSP
    true,   // RBP
#if PLATFORM_WINDOWS
    true,  // RSI
    true,  // RDI
#else
    false,  // RSI
    false,  // RDI
#endif
    false,  // R8
    false,  // R9
    false,  // R10
    false,  // R11
    true,   // R12
    true,   // R13
    true,   // R14
    true,   // R15
  };

  return callee_saved[reg.getIdx()];
}

static void x64_backend_emit_prolog(struct x64_backend *backend, struct ir *ir,
                                    int *out_stack_size) {
  auto &e = *backend->codegen;

  int stack_size = STACK_SIZE + ir->locals_size;

  // stack must be 16 byte aligned
  stack_size = align_up(stack_size, 16);

  // add 8 for return address which will be pushed when this is called
  stack_size += 8;

  CHECK_EQ((stack_size + 8) % 16, 0);

  // mark which registers have been modified
  memset(backend->modified, 0, sizeof(backend->modified));

  list_for_each_entry(instr, &ir->instrs, struct ir_instr, it) {
    struct ir_value *result = instr->result;

    if (!result) {
      continue;
    }

    backend->modified[result->reg] = true;
  }

  // push the callee-saved registers which have been modified
  int pushed = 2;

  // always used by guest ctx and memory pointers
  e.push(e.r15);
  e.push(e.r14);

  for (int i = 0; i < x64_num_registers; i++) {
    const Xbyak::Reg &reg =
        *reinterpret_cast<const Xbyak::Reg *>(x64_registers[i].data);

    if (x64_backend_callee_saved(reg) && backend->modified[i]) {
      e.push(reg);
      pushed++;
    }
  }

  // if an odd amount of push instructions are emitted stack_size needs to be
  // adjusted to keep the stack aligned
  if ((pushed % 2) == 1) {
    stack_size += 8;
  }

  // adjust stack pointer
  e.sub(e.rsp, stack_size);

  // copy guest context and memory base to argument registers
  e.mov(e.r14, reinterpret_cast<uint64_t>(backend->guest->ctx_base));
  e.mov(e.r15, reinterpret_cast<uint64_t>(backend->guest->mem_base));

  *out_stack_size = stack_size;
}

static void x64_backend_emit_body(struct x64_backend *backend, struct ir *ir) {
  list_for_each_entry(instr, &ir->instrs, struct ir_instr, it) {
    x64_emit_cb emit = x64_backend_emitters[instr->op];
    CHECK(emit, "Failed to find emitter for %s", ir_op_names[instr->op]);

    // reset temp count used by GetRegister
    backend->num_temps = 0;

    emit(backend, *backend->codegen, instr);
  }
}

static void x64_backend_emit_epilog(struct x64_backend *backend, struct ir *ir,
                                    int stack_size) {
  auto &e = *backend->codegen;

  // adjust stack pointer
  e.add(e.rsp, stack_size);

  // pop callee-saved registers which have been modified
  for (int i = x64_num_registers - 1; i >= 0; i--) {
    const Xbyak::Reg &reg =
        *reinterpret_cast<const Xbyak::Reg *>(x64_registers[i].data);

    if (x64_backend_callee_saved(reg) && backend->modified[i]) {
      e.pop(reg);
    }
  }

  // pop r14 and r15
  e.pop(e.r14);
  e.pop(e.r15);

  e.ret();
}

const uint8_t *x64_backend_emit(struct x64_backend *backend, struct ir *ir,
                                int *size) {
  // PROFILER_RUNTIME("X64Emitter::Emit");

  const uint8_t *fn = backend->codegen->getCurr();

  int stack_size = 0;
  x64_backend_emit_prolog(backend, ir, &stack_size);
  x64_backend_emit_body(backend, ir);
  x64_backend_emit_epilog(backend, ir, stack_size);

  *size = (int)(backend->codegen->getCurr() - fn);

  return fn;
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

  e.L(backend->xmm_const[XMM_CONST_ABS_MASK_PS]);
  e.dq(INT64_C(0x7fffffff7fffffff));
  e.dq(INT64_C(0x7fffffff7fffffff));

  e.L(backend->xmm_const[XMM_CONST_ABS_MASK_PD]);
  e.dq(INT64_C(0x7fffffffffffffff));
  e.dq(INT64_C(0x7fffffffffffffff));

  e.L(backend->xmm_const[XMM_CONST_SIGN_MASK_PS]);
  e.dq(INT64_C(0x8000000080000000));
  e.dq(INT64_C(0x8000000080000000));

  e.L(backend->xmm_const[XMM_CONST_SIGN_MASK_PD]);
  e.dq(INT64_C(0x8000000000000000));
  e.dq(INT64_C(0x8000000000000000));
}

static void x64_backend_reset(struct jit_backend *base) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  backend->codegen->reset();

  x64_backend_emit_thunks(backend);
  x64_backend_emit_constants(backend);
}

static const uint8_t *x64_backend_assemble_code(struct jit_backend *base,
                                                struct ir *ir, int *size) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  // try to generate the x64 code. if the code buffer overflows let the backend
  // know so it can reset the cache and try again
  const uint8_t *fn = nullptr;

  try {
    fn = x64_backend_emit(backend, ir, size);
  } catch (const Xbyak::Error &e) {
    if (e != Xbyak::ERR_CODE_IS_TOO_BIG) {
      LOG_FATAL("X64 codegen failure, %s", e.what());
    }
  }

  return fn;
}

static void x64_backend_dump_code(struct jit_backend *base,
                                  const uint8_t *host_addr, int size) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  cs_insn *insns;
  size_t count =
      cs_disasm(backend->capstone_handle, host_addr, size, 0, 0, &insns);
  CHECK(count);

  for (size_t i = 0; i < count; i++) {
    cs_insn &insn = insns[i];
    LOG_INFO("0x%" PRIx64 ":\t%s\t\t%s", insn.address, insn.mnemonic,
             insn.op_str);
  }

  cs_free(insns, count);
}

static bool x64_backend_handle_exception(struct jit_backend *base,
                                         struct exception *ex) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  const uint8_t *data = reinterpret_cast<const uint8_t *>(ex->thread_state.rip);

  // it's assumed a mov has triggered the exception
  struct x64_mov mov;
  if (!x64_decode_mov(data, &mov)) {
    return false;
  }

  // figure out the guest address that was being accessed
  const uint8_t *fault_addr = reinterpret_cast<const uint8_t *>(ex->fault_addr);
  const uint8_t *protected_start =
      reinterpret_cast<const uint8_t *>(backend->guest->mem_base);
  uint32_t guest_addr = static_cast<uint32_t>(fault_addr - protected_start);

  // instead of handling the dynamic callback from inside of the exception
  // handler, force rip to the beginning of a thunk which will invoke the
  // callback once the exception handler has exited. this frees the callbacks
  // from any restrictions imposed by an exception handler, and also prevents
  // a possible recursive exceptions

  // push the return address (the next instruction after the current mov) to
  // the stack. also, adjust the stack for the return address, with an extra
  // 8 bytes to keep it aligned
  *(uintptr_t *)(ex->thread_state.rsp - 8) = ex->thread_state.rip + mov.length;
  ex->thread_state.rsp -= STACK_SHADOW_SPACE + 8 + 8;
  CHECK(ex->thread_state.rsp % 16 == 0);

  if (mov.is_load) {
    // prep argument registers (memory object, guest_addr) for read function
    ex->thread_state.r[x64_arg0_idx] =
        reinterpret_cast<uint64_t>(backend->guest->mem_self);
    ex->thread_state.r[x64_arg1_idx] = static_cast<uint64_t>(guest_addr);

    // prep function call address for thunk
    switch (mov.operand_size) {
      case 1:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(backend->guest->r8);
        break;
      case 2:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(backend->guest->r16);
        break;
      case 4:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(backend->guest->r32);
        break;
      case 8:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(backend->guest->r64);
        break;
    }

    // resume execution in the thunk once the exception handler exits
    ex->thread_state.rip =
        reinterpret_cast<uint64_t>(backend->load_thunk[mov.reg]);
  } else {
    // prep argument registers (memory object, guest_addr, value) for write
    // function
    ex->thread_state.r[x64_arg0_idx] =
        reinterpret_cast<uint64_t>(backend->guest->mem_self);
    ex->thread_state.r[x64_arg1_idx] = static_cast<uint64_t>(guest_addr);
    ex->thread_state.r[x64_arg2_idx] = ex->thread_state.r[mov.reg];

    // prep function call address for thunk
    switch (mov.operand_size) {
      case 1:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(backend->guest->w8);
        break;
      case 2:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(backend->guest->w16);
        break;
      case 4:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(backend->guest->w32);
        break;
      case 8:
        ex->thread_state.rax = reinterpret_cast<uint64_t>(backend->guest->w64);
        break;
    }

    // resume execution in the thunk once the exception handler exits
    ex->thread_state.rip = reinterpret_cast<uint64_t>(backend->store_thunk);
  }

  return true;
}

EMITTER(LOAD_HOST) {
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (ir_is_float(instr->result->type)) {
    const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);

    switch (instr->result->type) {
      case VALUE_F32:
        e.vmovss(result, e.dword[a]);
        break;
      case VALUE_F64:
        e.vmovsd(result, e.qword[a]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else {
    const Xbyak::Reg result = x64_backend_register(backend, instr->result);

    switch (instr->result->type) {
      case VALUE_I8:
        e.mov(result, e.byte[a]);
        break;
      case VALUE_I16:
        e.mov(result, e.word[a]);
        break;
      case VALUE_I32:
        e.mov(result, e.dword[a]);
        break;
      case VALUE_I64:
        e.mov(result, e.qword[a]);
        break;
      default:
        LOG_FATAL("Unexpected load result type");
        break;
    }
  }
}

EMITTER(STORE_HOST) {
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (ir_is_float(instr->arg[1]->type)) {
    const Xbyak::Xmm b = x64_backend_xmm_register(backend, instr->arg[1]);

    switch (instr->arg[1]->type) {
      case VALUE_F32:
        e.vmovss(e.dword[a], b);
        break;
      case VALUE_F64:
        e.vmovsd(e.qword[a], b);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);

    switch (instr->arg[1]->type) {
      case VALUE_I8:
        e.mov(e.byte[a], b);
        break;
      case VALUE_I16:
        e.mov(e.word[a], b);
        break;
      case VALUE_I32:
        e.mov(e.dword[a], b);
        break;
      case VALUE_I64:
        e.mov(e.qword[a], b);
        break;
      default:
        LOG_FATAL("Unexpected store value type");
        break;
    }
  }
}

EMITTER(LOAD_FAST) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  switch (instr->result->type) {
    case VALUE_I8:
      e.mov(result, e.byte[a.cvt64() + e.r15]);
      break;
    case VALUE_I16:
      e.mov(result, e.word[a.cvt64() + e.r15]);
      break;
    case VALUE_I32:
      e.mov(result, e.dword[a.cvt64() + e.r15]);
      break;
    case VALUE_I64:
      e.mov(result, e.qword[a.cvt64() + e.r15]);
      break;
    default:
      LOG_FATAL("Unexpected load result type");
      break;
  }
}

EMITTER(STORE_FAST) {
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);
  const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);

  switch (instr->arg[1]->type) {
    case VALUE_I8:
      e.mov(e.byte[a.cvt64() + e.r15], b);
      break;
    case VALUE_I16:
      e.mov(e.word[a.cvt64() + e.r15], b);
      break;
    case VALUE_I32:
      e.mov(e.dword[a.cvt64() + e.r15], b);
      break;
    case VALUE_I64:
      e.mov(e.qword[a.cvt64() + e.r15], b);
      break;
    default:
      LOG_FATAL("Unexpected store value type");
      break;
  }
}

EMITTER(LOAD_SLOW) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  void *fn = nullptr;
  switch (instr->result->type) {
    case VALUE_I8:
      fn = reinterpret_cast<void *>(backend->guest->r8);
      break;
    case VALUE_I16:
      fn = reinterpret_cast<void *>(backend->guest->r16);
      break;
    case VALUE_I32:
      fn = reinterpret_cast<void *>(backend->guest->r32);
      break;
    case VALUE_I64:
      fn = reinterpret_cast<void *>(backend->guest->r64);
      break;
    default:
      LOG_FATAL("Unexpected load result type");
      break;
  }

  e.mov(arg0, reinterpret_cast<uint64_t>(backend->guest->mem_self));
  e.mov(arg1, a);
  e.call(reinterpret_cast<void *>(fn));
  e.mov(result, e.rax);
}

EMITTER(STORE_SLOW) {
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);
  const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);

  void *fn = nullptr;
  switch (instr->arg[1]->type) {
    case VALUE_I8:
      fn = reinterpret_cast<void *>(backend->guest->w8);
      break;
    case VALUE_I16:
      fn = reinterpret_cast<void *>(backend->guest->w16);
      break;
    case VALUE_I32:
      fn = reinterpret_cast<void *>(backend->guest->w32);
      break;
    case VALUE_I64:
      fn = reinterpret_cast<void *>(backend->guest->w64);
      break;
    default:
      LOG_FATAL("Unexpected store value type");
      break;
  }

  e.mov(arg0, reinterpret_cast<uint64_t>(backend->guest->mem_self));
  e.mov(arg1, a);
  e.mov(arg2, b);
  e.call(reinterpret_cast<void *>(fn));
}

EMITTER(LOAD_CONTEXT) {
  int offset = instr->arg[0]->i32;

  if (ir_is_vector(instr->result->type)) {
    const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);

    switch (instr->result->type) {
      case VALUE_V128:
        e.movups(result, e.ptr[e.r14 + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else if (ir_is_float(instr->result->type)) {
    const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);

    switch (instr->result->type) {
      case VALUE_F32:
        e.vmovss(result, e.dword[e.r14 + offset]);
        break;
      case VALUE_F64:
        e.vmovsd(result, e.qword[e.r14 + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else {
    const Xbyak::Reg result = x64_backend_register(backend, instr->result);

    switch (instr->result->type) {
      case VALUE_I8:
        e.mov(result, e.byte[e.r14 + offset]);
        break;
      case VALUE_I16:
        e.mov(result, e.word[e.r14 + offset]);
        break;
      case VALUE_I32:
        e.mov(result, e.dword[e.r14 + offset]);
        break;
      case VALUE_I64:
        e.mov(result, e.qword[e.r14 + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  }
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
    if (ir_is_vector(instr->arg[1]->type)) {
      const Xbyak::Xmm src = x64_backend_xmm_register(backend, instr->arg[1]);

      switch (instr->arg[1]->type) {
        case VALUE_V128:
          e.vmovups(e.ptr[e.r14 + offset], src);
          break;
        default:
          LOG_FATAL("Unexpected result type");
          break;
      }
    } else if (ir_is_float(instr->arg[1]->type)) {
      const Xbyak::Xmm src = x64_backend_xmm_register(backend, instr->arg[1]);

      switch (instr->arg[1]->type) {
        case VALUE_F32:
          e.vmovss(e.dword[e.r14 + offset], src);
          break;
        case VALUE_F64:
          e.vmovsd(e.qword[e.r14 + offset], src);
          break;
        default:
          LOG_FATAL("Unexpected value type");
          break;
      }
    } else {
      const Xbyak::Reg src = x64_backend_register(backend, instr->arg[1]);

      switch (instr->arg[1]->type) {
        case VALUE_I8:
          e.mov(e.byte[e.r14 + offset], src);
          break;
        case VALUE_I16:
          e.mov(e.word[e.r14 + offset], src);
          break;
        case VALUE_I32:
          e.mov(e.dword[e.r14 + offset], src);
          break;
        case VALUE_I64:
          e.mov(e.qword[e.r14 + offset], src);
          break;
        default:
          LOG_FATAL("Unexpected value type");
          break;
      }
    }
  }
}

EMITTER(LOAD_LOCAL) {
  int offset = STACK_OFFSET_LOCALS + instr->arg[0]->i32;

  if (ir_is_vector(instr->result->type)) {
    const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);

    switch (instr->result->type) {
      case VALUE_V128:
        e.movups(result, e.ptr[e.rsp + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else if (ir_is_float(instr->result->type)) {
    const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);

    switch (instr->result->type) {
      case VALUE_F32:
        e.vmovss(result, e.dword[e.rsp + offset]);
        break;
      case VALUE_F64:
        e.vmovsd(result, e.qword[e.rsp + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else {
    const Xbyak::Reg result = x64_backend_register(backend, instr->result);

    switch (instr->result->type) {
      case VALUE_I8:
        e.mov(result, e.byte[e.rsp + offset]);
        break;
      case VALUE_I16:
        e.mov(result, e.word[e.rsp + offset]);
        break;
      case VALUE_I32:
        e.mov(result, e.dword[e.rsp + offset]);
        break;
      case VALUE_I64:
        e.mov(result, e.qword[e.rsp + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  }
}

EMITTER(STORE_LOCAL) {
  int offset = STACK_OFFSET_LOCALS + instr->arg[0]->i32;

  CHECK(!ir_is_constant(instr->arg[1]));

  if (ir_is_vector(instr->arg[1]->type)) {
    const Xbyak::Xmm src = x64_backend_xmm_register(backend, instr->arg[1]);

    switch (instr->arg[1]->type) {
      case VALUE_V128:
        e.vmovups(e.ptr[e.rsp + offset], src);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else if (ir_is_float(instr->arg[1]->type)) {
    const Xbyak::Xmm src = x64_backend_xmm_register(backend, instr->arg[1]);

    switch (instr->arg[1]->type) {
      case VALUE_F32:
        e.vmovss(e.dword[e.rsp + offset], src);
        break;
      case VALUE_F64:
        e.vmovsd(e.qword[e.rsp + offset], src);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    const Xbyak::Reg src = x64_backend_register(backend, instr->arg[1]);

    switch (instr->arg[1]->type) {
      case VALUE_I8:
        e.mov(e.byte[e.rsp + offset], src);
        break;
      case VALUE_I16:
        e.mov(e.word[e.rsp + offset], src);
        break;
      case VALUE_I32:
        e.mov(e.dword[e.rsp + offset], src);
        break;
      case VALUE_I64:
        e.mov(e.qword[e.rsp + offset], src);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  }
}

EMITTER(FTOI) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);

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
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

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
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (a == result) {
    // already the correct width
    return;
  }

  if (result.isBit(64) && a.isBit(32)) {
    e.movsxd(result.cvt64(), a);
  } else {
    e.movsx(result, a);
  }
}

EMITTER(ZEXT) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (a == result) {
    // already the correct width
    return;
  }

  if (result.isBit(64) && a.isBit(32)) {
    // mov will automatically zero fill the upper 32-bits
    e.mov(result.cvt32(), a);
  } else {
    e.movzx(result, a);
  }
}

EMITTER(TRUNC) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result.getIdx() == a.getIdx()) {
    // noop if already the same register. note, this means the high order bits
    // of the result won't be cleared, but I believe that is fine
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
    // mov will automatically zero fill the upper 32-bits
    e.mov(result, truncated);
  } else {
    e.movzx(result.cvt32(), truncated);
  }
}

EMITTER(FEXT) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);

  e.cvtss2sd(result, a);
}

EMITTER(FTRUNC) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);

  e.cvtsd2ss(result, a);
}

EMITTER(SELECT) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);
  const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
  const Xbyak::Reg cond = x64_backend_register(backend, instr->arg[2]);

  // convert result to Reg32e to please xbyak
  CHECK_GE(result.getBit(), 32);
  Xbyak::Reg32e result_32e(result.getIdx(), result.getBit());

  e.test(cond, cond);
  if (result_32e != a) {
    e.cmovnz(result_32e, a);
  }
  e.cmovz(result_32e, b);
}

EMITTER(CMP) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.cmp(a, static_cast<uint32_t>(ir_zext_constant(instr->arg[1])));
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
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
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm_register(backend, instr->arg[1]);

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
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.add(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
    e.add(result, b);
  }
}

EMITTER(SUB) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.sub(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
    e.sub(result, b);
  }
}

EMITTER(SMUL) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);
  const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);

  if (result != a) {
    e.mov(result, a);
  }

  e.imul(result, b);
}

EMITTER(UMUL) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);
  const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);

  if (result != a) {
    e.mov(result, a);
  }

  e.imul(result, b);
}

EMITTER(DIV) {
  LOG_FATAL("Unsupported");
}

EMITTER(NEG) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  e.neg(result);
}

EMITTER(ABS) {
  LOG_FATAL("Unsupported");
  // e.mov(e.rax, *result);
  // e.neg(e.rax);
  // e.cmovl(reinterpret_cast<const Xbyak::Reg *>(result)->cvt32(), e.rax);
}

EMITTER(FADD) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm_register(backend, instr->arg[1]);

  if (instr->result->type == VALUE_F32) {
    e.vaddss(result, a, b);
  } else {
    e.vaddsd(result, a, b);
  }
}

EMITTER(FSUB) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm_register(backend, instr->arg[1]);

  if (instr->result->type == VALUE_F32) {
    e.vsubss(result, a, b);
  } else {
    e.vsubsd(result, a, b);
  }
}

EMITTER(FMUL) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm_register(backend, instr->arg[1]);

  if (instr->result->type == VALUE_F32) {
    e.vmulss(result, a, b);
  } else {
    e.vmulsd(result, a, b);
  }
}

EMITTER(FDIV) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm_register(backend, instr->arg[1]);

  if (instr->result->type == VALUE_F32) {
    e.vdivss(result, a, b);
  } else {
    e.vdivsd(result, a, b);
  }
}

EMITTER(FNEG) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);

  if (instr->result->type == VALUE_F32) {
    e.vxorps(result, a,
             x64_backend_xmm_constant(backend, XMM_CONST_SIGN_MASK_PS));
  } else {
    e.vxorpd(result, a,
             x64_backend_xmm_constant(backend, XMM_CONST_SIGN_MASK_PD));
  }
}

EMITTER(FABS) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);

  if (instr->result->type == VALUE_F32) {
    e.vandps(result, a,
             x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PS));
  } else {
    e.vandpd(result, a,
             x64_backend_xmm_constant(backend, XMM_CONST_ABS_MASK_PD));
  }
}

EMITTER(SQRT) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);

  if (instr->result->type == VALUE_F32) {
    e.vsqrtss(result, a);
  } else {
    e.vsqrtsd(result, a);
  }
}

EMITTER(VBROADCAST) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);

  e.vbroadcastss(result, a);
}

EMITTER(VADD) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm_register(backend, instr->arg[1]);

  e.vaddps(result, a, b);
}

EMITTER(VDOT) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm_register(backend, instr->arg[1]);

  e.vdpps(result, a, b, 0b11110001);
}

EMITTER(VMUL) {
  const Xbyak::Xmm result = x64_backend_xmm_register(backend, instr->result);
  const Xbyak::Xmm a = x64_backend_xmm_register(backend, instr->arg[0]);
  const Xbyak::Xmm b = x64_backend_xmm_register(backend, instr->arg[1]);

  e.vmulps(result, a, b);
}

EMITTER(AND) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.and_(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
    e.and_(result, b);
  }
}

EMITTER(OR) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.or_(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
    e.or_(result, b);
  }
}

EMITTER(XOR) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.xor_(result, (uint32_t)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
    e.xor_(result, b);
  }
}

EMITTER(NOT) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  e.not_(result);
}

EMITTER(SHL) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.shl(result, (int)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
    e.mov(e.cl, b);
    e.shl(result, e.cl);
  }
}

EMITTER(ASHR) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.sar(result, (int)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
    e.mov(e.cl, b);
    e.sar(result, e.cl);
  }
}

EMITTER(LSHR) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg a = x64_backend_register(backend, instr->arg[0]);

  if (result != a) {
    e.mov(result, a);
  }

  if (x64_backend_can_encode_imm(instr->arg[1])) {
    e.shr(result, (int)ir_zext_constant(instr->arg[1]));
  } else {
    const Xbyak::Reg b = x64_backend_register(backend, instr->arg[1]);
    e.mov(e.cl, b);
    e.shr(result, e.cl);
  }
}

EMITTER(ASHD) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg v = x64_backend_register(backend, instr->arg[0]);
  const Xbyak::Reg n = x64_backend_register(backend, instr->arg[1]);

  e.inLocalLabel();

  if (result != v) {
    e.mov(result, v);
  }

  // check if we're shifting left or right
  e.test(n, 0x80000000);
  e.jnz(".shr");

  // perform shift left
  e.mov(e.cl, n);
  e.sal(result, e.cl);
  e.jmp(".end");

  // perform right shift
  e.L(".shr");
  e.test(n, 0x1f);
  e.jz(".shr_overflow");
  e.mov(e.cl, n);
  e.neg(e.cl);
  e.sar(result, e.cl);
  e.jmp(".end");

  // right shift overflowed
  e.L(".shr_overflow");
  e.sar(result, 31);

  // shift is done
  e.L(".end");

  e.outLocalLabel();
}

EMITTER(LSHD) {
  const Xbyak::Reg result = x64_backend_register(backend, instr->result);
  const Xbyak::Reg v = x64_backend_register(backend, instr->arg[0]);
  const Xbyak::Reg n = x64_backend_register(backend, instr->arg[1]);

  e.inLocalLabel();

  if (result != v) {
    e.mov(result, v);
  }

  // check if we're shifting left or right
  e.test(n, 0x80000000);
  e.jnz(".shr");

  // perform shift left
  e.mov(e.cl, n);
  e.shl(result, e.cl);
  e.jmp(".end");

  // perform right shift
  e.L(".shr");
  e.test(n, 0x1f);
  e.jz(".shr_overflow");
  e.mov(e.cl, n);
  e.neg(e.cl);
  e.shr(result, e.cl);
  e.jmp(".end");

  // right shift overflowed
  e.L(".shr_overflow");
  e.mov(result, 0x0);

  // shift is done
  e.L(".end");

  e.outLocalLabel();
}

EMITTER(CALL_EXTERNAL) {
  const Xbyak::Reg addr = x64_backend_register(backend, instr->arg[0]);

  e.mov(arg0, reinterpret_cast<uint64_t>(backend->guest->ctx_base));
  if (instr->arg[1]) {
    const Xbyak::Reg arg = x64_backend_register(backend, instr->arg[1]);
    e.mov(arg1, arg);
  }
  e.mov(e.rax, addr);
  e.call(e.rax);
}

EMITTER(CALL_FALLBACK) {
  void *fallback = (void *)instr->arg[0]->i64;
  uint32_t addr = instr->arg[1]->i32;
  uint32_t raw_instr = instr->arg[2]->i32;

  e.mov(arg0, reinterpret_cast<uint64_t>(backend->guest));
  e.mov(arg1, addr);
  e.mov(arg2, raw_instr);
  e.mov(e.rax, reinterpret_cast<uint64_t>(fallback));
  e.call(e.rax);
}

struct jit_backend *x64_backend_create(const struct jit_guest *guest) {
  struct x64_backend *backend = reinterpret_cast<struct x64_backend *>(
      calloc(1, sizeof(struct x64_backend)));

  backend->base.registers = x64_registers;
  backend->base.num_registers = array_size(x64_registers);
  backend->base.reset = &x64_backend_reset;
  backend->base.assemble_code = &x64_backend_assemble_code;
  backend->base.dump_code = &x64_backend_dump_code;
  backend->base.handle_exception = &x64_backend_handle_exception;

  backend->guest = guest;

  backend->codegen = new Xbyak::CodeGenerator(x64_code_size, x64_code);
  Xbyak::CodeArray::protect(x64_code, x64_code_size, true);

  int res = cs_open(CS_ARCH_X86, CS_MODE_64, &backend->capstone_handle);
  CHECK_EQ(res, CS_ERR_OK);

  // make the code buffer executable
  int page_size = (int)get_page_size();
  void *aligned_code = (void *)align_down((intptr_t)x64_code, page_size);
  int aligned_code_size = align_up(x64_code_size, page_size);
  bool success =
      protect_pages(aligned_code, aligned_code_size, ACC_READWRITEEXEC);
  CHECK(success);

  // do an initial reset to emit constants and thinks
  x64_backend_reset((jit_backend *)backend);

  return (struct jit_backend *)backend;
}

void x64_backend_destroy(struct jit_backend *jit_backend) {
  struct x64_backend *backend = (struct x64_backend *)jit_backend;

  cs_close(&backend->capstone_handle);

  delete backend->codegen;

  free(backend);
}
