#include "core/core.h"
#include "emu/profiler.h"
#include "jit/backend/x64/x64_emitter.h"

using namespace dreavm;
using namespace dreavm::hw;
using namespace dreavm::jit::backend::x64;
using namespace dreavm::jit::ir;

//
// x64 register layout
//

// %rax %eax %ax %al      <-- temporary
// %rcx %ecx %cx %cl      <-- argument
// %rdx %edx %dx %dl      <-- argument
// %rbx %ebx %bx %bl      <-- available, callee saved
// %rsi %esi %si %sil     <-- argument
// %rdi %edi %di %dil     <-- argument
// %rsp %esp %sp %spl     <-- reserved
// %rbp %ebp %bp %bpl     <-- available, callee saved
// %r8 %r8d %r8w %r8b     <-- argument
// %r9 %r9d %r9w %r9b     <-- argument
// %r10 %r10d %r10w %r10b <-- available, not callee saved
// %r11 %r11d %r11w %r11b <-- available, not callee saved
// %r12 %r12d %r12w %r12b <-- available, callee saved
// %r13 %r13d %r13w %r13b <-- available, callee saved
// %r14 %r14d %r14w %r14b <-- available, callee saved
// %r15 %r15d %r15w %r15b <-- available, callee saved

// msvc calling convention uses rcx, rdx, r8 and r9 for arguments
// amd64 calling convention uses rdi, rsi, rdx, rcx, r8 and r9 for arguments
// both use the same xmm registers for floating point arguments
// our largest function call uses only 3 arguments, leaving rdi, rsi and r9
// available on msvc and rcx, r8 and r8 available on amd64

// rax is used as a scratch register, while r9 and xmm1 are used for storing
// a constant in case the constant propagation pass didn't eliminate it

// rdi, rsi are left unused on msvc and rcx, r8 is left unused on amd64

// map register ids coming from IR values, must be in sync with x64_backend.h
// note, only callee saved registers are used here to avoid having to reload
// registers when calling non-JIT'd functions
static const Xbyak::Reg *reg_map_8[] = {
    &Xbyak::util::bl, &Xbyak::util::bpl, &Xbyak::util::r12b, &Xbyak::util::r13b,
    &Xbyak::util::r14b, &Xbyak::util::r15b, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr};
static const Xbyak::Reg *reg_map_16[] = {
    &Xbyak::util::bx, &Xbyak::util::bp, &Xbyak::util::r12w, &Xbyak::util::r13w,
    &Xbyak::util::r14w, &Xbyak::util::r15w, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr};
static const Xbyak::Reg *reg_map_32[] = {
    &Xbyak::util::ebx,  &Xbyak::util::ebp,   &Xbyak::util::r12d,
    &Xbyak::util::r13d, &Xbyak::util::r14d,  &Xbyak::util::r15d,
    &Xbyak::util::xmm6, &Xbyak::util::xmm7,  &Xbyak::util::xmm8,
    &Xbyak::util::xmm9, &Xbyak::util::xmm10, &Xbyak::util::xmm11};
static const Xbyak::Reg *reg_map_64[] = {
    &Xbyak::util::rbx,  &Xbyak::util::rbp,   &Xbyak::util::r12,
    &Xbyak::util::r13,  &Xbyak::util::r14,   &Xbyak::util::r15,
    &Xbyak::util::xmm6, &Xbyak::util::xmm7,  &Xbyak::util::xmm8,
    &Xbyak::util::xmm9, &Xbyak::util::xmm10, &Xbyak::util::xmm11};

#ifdef PLATFORM_WINDOWS
static const Xbyak::Reg &int_arg0 = Xbyak::util::rcx;
static const Xbyak::Reg &int_arg1 = Xbyak::util::rdx;
static const Xbyak::Reg &int_arg2 = Xbyak::util::r8;
#else
static const Xbyak::Reg &int_arg0 = Xbyak::util::rdi;
static const Xbyak::Reg &int_arg1 = Xbyak::util::rsi;
static const Xbyak::Reg &int_arg2 = Xbyak::util::rdx;
#endif

// get and set Xbyak::Labels in the IR block's tag
#define GetLabel(blk) (*reinterpret_cast<Xbyak::Label *>(blk->tag()))
#define SetLabel(blk, lbl) (blk->set_tag(reinterpret_cast<intptr_t>(lbl)))

// callbacks for emitting each IR op
typedef void (*X64Emit)(X64Emitter &, Memory &, Xbyak::CodeGenerator &,
                        const Instr *);

static X64Emit x64_emitters[NUM_OPCODES];

#define EMITTER(op)                                               \
  void op(X64Emitter &e, Memory &memory, Xbyak::CodeGenerator &c, \
          const Instr *instr);                                    \
  static struct _x64_##op##_init {                                \
    _x64_##op##_init() { x64_emitters[OP_##op] = &op; }           \
  } x64_##op##_init;                                              \
  void op(X64Emitter &e, Memory &memory, Xbyak::CodeGenerator &c, \
          const Instr *instr)

X64Emitter::X64Emitter(Memory &memory)
    : memory_(memory), c_(1024 * 1024 * 8), arena_(1024) {}

void X64Emitter::Reset() { c_.reset(); }

bool X64Emitter::Emit(IRBuilder &builder, X64Fn *fn) {
  PROFILER_RUNTIME("X64Emitter::Emit");

  // getCurr returns the current spot in the codegen buffer which the function
  // is about to emitted to
  *fn = c_.getCurr<X64Fn>();

  // reset arena holding temporaries used while emitting
  arena_.Reset();

  // allocate the epilog label
  epilog_label_ = AllocLabel();

  // assign local offsets
  int stack_size = STACK_SIZE;
  for (auto local : builder.locals()) {
    int type_size = SizeForType(local->type());
    stack_size = dreavm::align(stack_size, type_size);
    local->set_offset(builder.AllocConstant(stack_size));
    stack_size += type_size;
  }

  // stack must be 16 byte aligned
  stack_size = dreavm::align(stack_size, 16);

  // add 8 for return address which will be pushed when this is called
  stack_size += 8;

// emit prolog
// FIXME only push registers that're used
#ifdef PLATFORM_WINDOWS
  c_.push(Xbyak::util::rdi);
  c_.push(Xbyak::util::rsi);
#endif
  c_.push(Xbyak::util::rbx);
  c_.push(Xbyak::util::rbp);
  c_.push(Xbyak::util::r12);
  c_.push(Xbyak::util::r13);
  c_.push(Xbyak::util::r14);
  c_.push(Xbyak::util::r15);

  // reserve stack space for rdi copy
  c_.sub(Xbyak::util::rsp, stack_size);
  c_.mov(c_.qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT], int_arg0);
  c_.mov(c_.qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY], int_arg1);

  // generate labels for each block
  for (auto block : builder.blocks()) {
    Xbyak::Label *lbl = AllocLabel();
    SetLabel(block, lbl);
  }

  // emit each instruction
  for (auto block : builder.blocks()) {
    c_.L(GetLabel(block));

    for (auto instr : block->instrs()) {
      X64Emit emit = x64_emitters[instr->op()];
      CHECK(emit, "Failed to find emitter for %s", Opnames[instr->op()]);

      // try to generate the x64 code. if the codegen buffer overflows let the
      // backend know so it can reset the cache and try again
      try {
        emit(*this, memory_, c_, instr);
      } catch (const Xbyak::Error &e) {
        if (e == Xbyak::ERR_CODE_IS_TOO_BIG) {
          return false;
        }

        LOG_FATAL("X64 codegen failure, %s", e.what());
      }
    }
  }

  // emit prolog
  c_.L(epilog_label());

  // reset stack
  c_.add(Xbyak::util::rsp, stack_size);

  // TODO only pop registers that're used
  c_.pop(Xbyak::util::r15);
  c_.pop(Xbyak::util::r14);
  c_.pop(Xbyak::util::r13);
  c_.pop(Xbyak::util::r12);
  c_.pop(Xbyak::util::rbp);
  c_.pop(Xbyak::util::rbx);
#ifdef PLATFORM_WINDOWS
  c_.pop(Xbyak::util::rsi);
  c_.pop(Xbyak::util::rdi);
#endif
  c_.ret();

  // patch up relocations
  c_.ready();

  return true;
}

Xbyak::Label *X64Emitter::AllocLabel() {
  Xbyak::Label *label = arena_.Alloc<Xbyak::Label>();
  new (label) Xbyak::Label();
  return label;
}

Xbyak::Address *X64Emitter::AllocAddress(const Xbyak::Address &from) {
  Xbyak::Address *addr = arena_.Alloc<Xbyak::Address>();
  new (addr) Xbyak::Address(from);
  return addr;
}

// Get the register / local allocated for the supplied value. If the value is
// a constant, copy it to a temporary register. The size argument can be
// overridden to get a truncated version of the value.
const Xbyak::Operand &X64Emitter::GetOperand(const Value *v, int size) {
  if (size == -1) {
    size = SizeForType(v->type());
  }

  if (v->reg() != NO_REGISTER) {
    const Xbyak::Reg *reg = nullptr;

    switch (size) {
      case 8:
        reg = reg_map_64[v->reg()];
        break;
      case 4:
        reg = reg_map_32[v->reg()];
        break;
      case 2:
        reg = reg_map_16[v->reg()];
        break;
      case 1:
        reg = reg_map_8[v->reg()];
        break;
    }

    CHECK_NOTNULL(reg);

    return *reg;
  }

  LOG_FATAL("Unexpected operand type");
}

// If the value is a local or constant, copy it to a tempory register, else
// return the register allocated for it.
const Xbyak::Reg &X64Emitter::GetRegister(const Value *v) {
  if (v->constant()) {
    const Xbyak::Reg *tmp = nullptr;

    switch (v->type()) {
      case VALUE_I8:
        tmp = &c_.r9b;
        break;
      case VALUE_I16:
        tmp = &c_.r9w;
        break;
      case VALUE_I32:
        tmp = &c_.r9d;
        break;
      case VALUE_I64:
        tmp = &c_.r9;
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }

    // copy value to the temporary register
    CopyOperand(v, *tmp);

    return *tmp;
  }

  const Xbyak::Operand &op = GetOperand(v);
  CHECK(op.isREG());
  return reinterpret_cast<const Xbyak::Reg &>(op);
}

// If the value isn't allocated a XMM register copy it to a temporary XMM,
// register, else return the XMM register allocated for it.
const Xbyak::Xmm &X64Emitter::GetXMMRegister(const Value *v) {
  if (v->constant()) {
    CopyOperand(v, c_.xmm1);
    return c_.xmm1;
  }

  const Xbyak::Operand &op = GetOperand(v);
  CHECK(op.isXMM());
  return reinterpret_cast<const Xbyak::Xmm &>(op);
}

// Copy the value to the supplied operand.
const Xbyak::Operand &X64Emitter::CopyOperand(const Value *v,
                                              const Xbyak::Operand &to) {
  if (v->constant()) {
    if (to.isXMM()) {
      CHECK(IsFloatType(v->type()));

      if (v->type() == VALUE_F32) {
        float val = v->value<float>();
        c_.mov(c_.eax, *reinterpret_cast<int32_t *>(&val));
        c_.movd(reinterpret_cast<const Xbyak::Xmm &>(to), c_.eax);
      } else {
        double val = v->value<double>();
        c_.mov(c_.rax, *reinterpret_cast<int64_t *>(&val));
        c_.movq(reinterpret_cast<const Xbyak::Xmm &>(to), c_.rax);
      }
    } else {
      CHECK(IsIntType(v->type()));

      c_.mov(to, v->GetZExtValue());
    }
  } else {
    const Xbyak::Operand &from = GetOperand(v);

    if (from == to) {
      return to;
    }

    // shouldn't ever be doing this
    CHECK(!from.isREG() || !to.isREG() || from.getIdx() != to.getIdx() ||
              from.getKind() != to.getKind(),
          "Unexpected copy operation between the same register of different "
          "sizes");

    if (to.isXMM()) {
      if (from.isXMM()) {
        c_.movdqa(reinterpret_cast<const Xbyak::Xmm &>(to), from);
      } else if (from.isBit(32)) {
        c_.movss(reinterpret_cast<const Xbyak::Xmm &>(to), from);
      } else if (from.isBit(64)) {
        c_.movsd(reinterpret_cast<const Xbyak::Xmm &>(to), from);
      } else {
        LOG_FATAL("Unexpected copy");
      }
    } else if (from.isXMM()) {
      CHECK(to.isMEM(), "Expected destination to be a memory address");

      if (to.isBit(32)) {
        c_.movss(reinterpret_cast<const Xbyak::Address &>(to),
                 reinterpret_cast<const Xbyak::Xmm &>(from));
      } else if (to.isBit(64)) {
        c_.movsd(reinterpret_cast<const Xbyak::Address &>(to),
                 reinterpret_cast<const Xbyak::Xmm &>(from));
      } else {
        LOG_FATAL("Unexpected copy");
      }
    } else {
      c_.mov(to, from);
    }
  }

  return to;
}

bool X64Emitter::CanEncodeAsImmediate(const Value *v) const {
  if (!v->constant()) {
    return false;
  }

  return v->type() <= VALUE_I32;
}

void X64Emitter::RestoreArg0() {
  c_.mov(int_arg0, c_.qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT]);
}

void X64Emitter::RestoreArg1() {
  c_.mov(int_arg1, c_.qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY]);
}

void X64Emitter::RestoreArgs() {
  RestoreArg0();
  RestoreArg1();
}

EMITTER(LOAD_CONTEXT) {
  int offset = instr->arg0()->value<int32_t>();

  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());

    switch (instr->result()->type()) {
      case VALUE_F32:
        c.movss(result, c.dword[int_arg0 + offset]);
        break;
      case VALUE_F64:
        c.movsd(result, c.qword[int_arg0 + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());

    switch (instr->result()->type()) {
      case VALUE_I8:
        c.mov(result, c.byte[int_arg0 + offset]);
        break;
      case VALUE_I16:
        c.mov(result, c.word[int_arg0 + offset]);
        break;
      case VALUE_I32:
        c.mov(result, c.dword[int_arg0 + offset]);
        break;
      case VALUE_I64:
        c.mov(result, c.qword[int_arg0 + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  }
}

EMITTER(STORE_CONTEXT) {
  int offset = instr->arg0()->value<int32_t>();

  if (instr->arg1()->constant()) {
    switch (instr->arg1()->type()) {
      case VALUE_I8:
        c.mov(c.byte[int_arg0 + offset], instr->arg1()->value<int8_t>());
        break;
      case VALUE_I16:
        c.mov(c.word[int_arg0 + offset], instr->arg1()->value<int16_t>());
        break;
      case VALUE_I32:
      case VALUE_F32:
        c.mov(c.dword[int_arg0 + offset], instr->arg1()->value<int32_t>());
        break;
      case VALUE_I64:
      case VALUE_F64:
        c.mov(c.qword[int_arg0 + offset], instr->arg1()->value<int64_t>());
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    if (IsFloatType(instr->arg1()->type())) {
      const Xbyak::Xmm &src = e.GetXMMRegister(instr->arg1());

      switch (instr->arg1()->type()) {
        case VALUE_F32:
          c.movss(c.dword[int_arg0 + offset], src);
          break;
        case VALUE_F64:
          c.movsd(c.qword[int_arg0 + offset], src);
          break;
        default:
          LOG_FATAL("Unexpected value type");
          break;
      }
    } else {
      const Xbyak::Reg &src = e.GetRegister(instr->arg1());

      switch (instr->arg1()->type()) {
        case VALUE_I8:
          c.mov(c.byte[int_arg0 + offset], src);
          break;
        case VALUE_I16:
          c.mov(c.word[int_arg0 + offset], src);
          break;
        case VALUE_I32:
          c.mov(c.dword[int_arg0 + offset], src);
          break;
        case VALUE_I64:
          c.mov(c.qword[int_arg0 + offset], src);
          break;
        default:
          LOG_FATAL("Unexpected value type");
          break;
      }
    }
  }
}

EMITTER(LOAD_LOCAL) {
  int offset = instr->arg0()->value<int32_t>();

  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());

    switch (instr->result()->type()) {
      case VALUE_F32:
        c.movss(result, c.dword[c.rsp + offset]);
        break;
      case VALUE_F64:
        c.movsd(result, c.qword[c.rsp + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());

    switch (instr->result()->type()) {
      case VALUE_I8:
        c.mov(result, c.byte[c.rsp + offset]);
        break;
      case VALUE_I16:
        c.mov(result, c.word[c.rsp + offset]);
        break;
      case VALUE_I32:
        c.mov(result, c.dword[c.rsp + offset]);
        break;
      case VALUE_I64:
        c.mov(result, c.qword[c.rsp + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  }
}

EMITTER(STORE_LOCAL) {
  int offset = instr->arg0()->value<int32_t>();

  CHECK(!instr->arg1()->constant());

  if (IsFloatType(instr->arg1()->type())) {
    const Xbyak::Xmm &src = e.GetXMMRegister(instr->arg1());

    switch (instr->arg1()->type()) {
      case VALUE_F32:
        c.movss(c.dword[c.rsp + offset], src);
        break;
      case VALUE_F64:
        c.movsd(c.qword[c.rsp + offset], src);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    const Xbyak::Reg &src = e.GetRegister(instr->arg1());

    switch (instr->arg1()->type()) {
      case VALUE_I8:
        c.mov(c.byte[c.rsp + offset], src);
        break;
      case VALUE_I16:
        c.mov(c.word[c.rsp + offset], src);
        break;
      case VALUE_I32:
        c.mov(c.dword[c.rsp + offset], src);
        break;
      case VALUE_I64:
        c.mov(c.qword[c.rsp + offset], src);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  }
}

EMITTER(LOAD) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (instr->arg0()->constant()) {
    // try to resolve the address to a physical page
    int32_t addr = instr->arg0()->value<int32_t>();
    MemoryBank *bank = nullptr;
    uint32_t offset = 0;

    memory.Resolve(addr, &bank, &offset);

    // if the address maps to a physical page, not a dynamic handler, let's
    // make it fast
    if (bank->physical_addr) {
      // FIXME it'd be nice if xbyak had a mov operation which would convert
      // the displacement to a RIP-relative address when finalizing code so
      // we didn't have to store the absolute address in the scratch register
      void *physical_addr = bank->physical_addr + offset;
      c.mov(c.rax, (size_t)physical_addr);

      switch (instr->result()->type()) {
        case VALUE_I8:
          c.mov(result, c.byte[c.rax]);
          break;
        case VALUE_I16:
          c.mov(result, c.word[c.rax]);
          break;
        case VALUE_I32:
          c.mov(result, c.dword[c.rax]);
          break;
        case VALUE_I64:
          c.mov(result, c.qword[c.rax]);
          break;
        default:
          LOG_FATAL("Unexpected load result type");
          break;
      }

      return;
    }
  }

  void *fn = nullptr;
  switch (instr->result()->type()) {
    case VALUE_I8:
      fn = reinterpret_cast<void *>(
          static_cast<uint8_t (*)(Memory *, uint32_t)>(&Memory::R8));
      break;
    case VALUE_I16:
      fn = reinterpret_cast<void *>(
          static_cast<uint16_t (*)(Memory *, uint32_t)>(&Memory::R16));
      break;
    case VALUE_I32:
      fn = reinterpret_cast<void *>(
          static_cast<uint32_t (*)(Memory *, uint32_t)>(&Memory::R32));
      break;
    case VALUE_I64:
      fn = reinterpret_cast<void *>(
          static_cast<uint64_t (*)(Memory *, uint32_t)>(&Memory::R64));
      break;
    default:
      LOG_FATAL("Unexpected load result type");
      break;
  }

  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  // setup arguments
  c.mov(int_arg0, int_arg1);
  c.mov(int_arg1, a);

  // call func
  c.mov(c.rax, (uintptr_t)fn);
  c.call(c.rax);

  // copy off result
  c.mov(result, c.rax);

  e.RestoreArgs();
}

EMITTER(STORE) {
  if (instr->arg0()->constant()) {
    // try to resolve the address to a physical page
    int32_t addr = instr->arg0()->value<int32_t>();
    MemoryBank *bank = nullptr;
    uint32_t offset = 0;

    memory.Resolve(addr, &bank, &offset);

    // if the address maps to a physical page, not a dynamic handler, let's
    // make it fast
    if (bank->physical_addr) {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());

      // FIXME it'd be nice if xbyak had a mov operation which would convert
      // the displacement to a RIP-relative address when finalizing code so
      // we didn't have to store the absolute address in the scratch register
      void *physical_addr = bank->physical_addr + offset;
      c.mov(c.rax, (size_t)physical_addr);

      switch (instr->arg1()->type()) {
        case VALUE_I8:
          c.mov(c.byte[c.rax], b);
          break;
        case VALUE_I16:
          c.mov(c.word[c.rax], b);
          break;
        case VALUE_I32:
          c.mov(c.dword[c.rax], b);
          break;
        case VALUE_I64:
          c.mov(c.qword[c.rax], b);
          break;
        default:
          LOG_FATAL("Unexpected store value type");
          break;
      }

      return;
    }
  }

  void *fn = nullptr;
  switch (instr->arg1()->type()) {
    case VALUE_I8:
      fn = reinterpret_cast<void *>(
          static_cast<void (*)(Memory *, uint32_t, uint8_t)>(&Memory::W8));
      break;
    case VALUE_I16:
      fn = reinterpret_cast<void *>(
          static_cast<void (*)(Memory *, uint32_t, uint16_t)>(&Memory::W16));
      break;
    case VALUE_I32:
      fn = reinterpret_cast<void *>(
          static_cast<void (*)(Memory *, uint32_t, uint32_t)>(&Memory::W32));
      break;
    case VALUE_I64:
      fn = reinterpret_cast<void *>(
          static_cast<void (*)(Memory *, uint32_t, uint64_t)>(&Memory::W64));
      break;
    default:
      LOG_FATAL("Unexpected store value type");
      break;
  }

  const Xbyak::Reg &a = e.GetRegister(instr->arg0());
  const Xbyak::Reg &b = e.GetRegister(instr->arg1());

  // setup arguments
  c.mov(int_arg0, int_arg1);
  c.mov(int_arg1, a);
  c.mov(int_arg2, b);

  // call func
  c.mov(c.rax, (uintptr_t)fn);
  c.call(c.rax);

  e.RestoreArgs();
}

EMITTER(CAST) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    switch (instr->result()->type()) {
      case VALUE_F32:
        CHECK_EQ(instr->arg0()->type(), VALUE_I32);
        c.cvtsi2ss(result, a);
        break;
      case VALUE_F64:
        CHECK_EQ(instr->arg0()->type(), VALUE_I64);
        c.cvtsi2sd(result, a);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

    switch (instr->result()->type()) {
      case VALUE_I32:
        CHECK_EQ(instr->arg0()->type(), VALUE_F32);
        c.cvttss2si(result, a);
        break;
      case VALUE_I64:
        CHECK_EQ(instr->arg0()->type(), VALUE_F64);
        c.cvttsd2si(result, a);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  }
}

EMITTER(SEXT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (a == result) {
    // already the correct width
    return;
  }

  if (result.isBit(64) && a.isBit(32)) {
    c.movsxd(result.cvt64(), a);
  } else {
    c.movsx(result, a);
  }
}

EMITTER(ZEXT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (a == result) {
    // already the correct width
    return;
  }

  if (result.isBit(64)) {
    // mov will automatically zero fill the upper 32-bits
    c.mov(result.cvt32(), a);
  } else {
    c.movzx(result, a);
  }
}

EMITTER(TRUNCATE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (a == result) {
    // already the correct width
    return;
  }

  Xbyak::Reg truncated = a;
  switch (instr->result()->type()) {
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
      LOG_FATAL("Unexpected truncation result size");
  }

  if (truncated.isBit(32)) {
    // mov will automatically zero fill the upper 32-bits
    c.mov(result, truncated);
  } else {
    c.movzx(result.cvt32(), truncated);
  }
}

EMITTER(SELECT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &cond = e.GetRegister(instr->arg0());
  const Xbyak::Reg &a = e.GetRegister(instr->arg1());
  const Xbyak::Reg &b = e.GetRegister(instr->arg2());

  c.test(cond, cond);
  c.cmovnz(result.cvt32(), a);
  c.cmovz(result.cvt32(), b);
}

EMITTER(EQ) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      c.cmp(a, b);
    }
  }

  c.sete(result);
}

EMITTER(NE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      c.cmp(a, b);
    }
  }

  c.setne(result);
}

EMITTER(SGE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }

    c.setae(result);
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      c.cmp(a, b);
    }

    c.setge(result);
  }
}

EMITTER(SGT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }

    c.seta(result);
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      c.cmp(a, b);
    }

    c.setg(result);
  }
}

EMITTER(UGE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.cmp(a, b);
  }

  c.setae(result);
}

EMITTER(UGT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.cmp(a, b);
  }

  c.seta(result);
}

EMITTER(SLE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }

    c.setbe(result);
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      c.cmp(a, b);
    }

    c.setle(result);
  }
}

EMITTER(SLT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }

    c.setb(result);
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      c.cmp(a, b);
    }

    c.setl(result);
  }
}

EMITTER(ULE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.cmp(a, b);
  }

  c.setbe(result);
}

EMITTER(ULT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.cmp(a, b);
  }

  c.setb(result);
}

EMITTER(ADD) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->result()->type() == VALUE_F32) {
      if (result != a) {
        c.movss(result, a);
      }

      c.addss(result, b);
    } else {
      if (result != a) {
        c.movsd(result, a);
      }

      c.addsd(result, b);
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (result != a) {
      c.mov(result, a);
    }

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.add(result, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      c.add(result, b);
    }
  }
}

EMITTER(SUB) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->result()->type() == VALUE_F32) {
      if (result != a) {
        c.movss(result, a);
      }

      c.subss(result, b);
    } else {
      if (result != a) {
        c.movsd(result, a);
      }

      c.subsd(result, b);
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (result != a) {
      c.mov(result, a);
    }

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.sub(result, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      c.sub(result, b);
    }
  }
}

EMITTER(SMUL) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->result()->type() == VALUE_F32) {
      if (result != a) {
        c.movss(result, a);
      }

      c.mulss(result, b);
    } else {
      if (result != a) {
        c.movsd(result, a);
      }

      c.mulsd(result, b);
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());

    if (result != a) {
      c.mov(result, a);
    }

    c.imul(result, b);
  }
}

EMITTER(UMUL) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());
  const Xbyak::Reg &b = e.GetRegister(instr->arg1());

  if (result != a) {
    c.mov(result, a);
  }

  c.imul(result, b);
}

EMITTER(DIV) {
  CHECK(IsFloatType(instr->result()->type()));

  const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
  const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
  const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

  if (instr->result()->type() == VALUE_F32) {
    if (result != a) {
      c.movss(result, a);
    }

    c.divss(result, b);
  } else {
    if (result != a) {
      c.movsd(result, a);
    }

    c.divsd(result, b);
  }
}

EMITTER(NEG) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

    if (instr->result()->type() == VALUE_F32) {
      // TODO use xorps
      c.movd(c.eax, a);
      c.xor (c.eax, (uint32_t)0x80000000);
      c.movd(result, c.eax);
    } else {
      // TODO use xorpd
      c.movq(c.rax, a);
      c.mov(c.r9, (uint64_t)0x8000000000000000);
      c.xor (c.rax, c.r9);
      c.movq(result, c.rax);
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (result != a) {
      c.mov(result, a);
    }

    c.neg(result);
  }
}

EMITTER(SQRT) {
  CHECK(IsFloatType(instr->result()->type()));

  const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
  const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

  if (instr->result()->type() == VALUE_F32) {
    c.sqrtss(result, a);
  } else {
    c.sqrtsd(result, a);
  }
}

EMITTER(ABS) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

    if (instr->result()->type() == VALUE_F32) {
      // TODO use andps
      c.movd(c.eax, a);
      c.and (c.eax, (uint32_t)0x7fffffff);
      c.movd(result, c.eax);
    } else {
      // TODO use andpd
      c.movq(c.rax, a);
      c.mov(c.r9, (uint64_t)0x7fffffffffffffff);
      c.and (c.rax, c.r9);
      c.movq(result, c.rax);
    }
  } else {
    LOG_FATAL("Unexpected abs result type");
    // c.mov(c.rax, *result);
    // c.neg(c.rax);
    // c.cmovl(reinterpret_cast<const Xbyak::Reg *>(result)->cvt32(), c.rax);
  }
}

EMITTER(SIN) {
  CHECK(IsFloatType(instr->result()->type()));

  const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
  const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

  // FIXME xmm registers aren't callee saved, this would probably break if we
  // used the lower indexed xmm registers
  if (instr->result()->type() == VALUE_F32) {
    c.movss(c.xmm0, a);
    c.mov(c.rax, (uint64_t) reinterpret_cast<float (*)(float)>(&sinf));
    c.call(c.rax);
    c.movss(result, c.xmm0);
  } else {
    c.movsd(c.xmm0, a);
    c.mov(c.rax, (uint64_t) reinterpret_cast<double (*)(double)>(&sin));
    c.call(c.rax);
    c.movsd(result, c.xmm0);
  }

  e.RestoreArgs();
}

EMITTER(COS) {
  CHECK(IsFloatType(instr->result()->type()));

  const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
  const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

  // FIXME xmm registers aren't callee saved, this would probably break if we
  // used the lower indexed xmm registers
  if (instr->result()->type() == VALUE_F32) {
    c.movss(c.xmm0, a);
    c.mov(c.rax, (uint64_t) reinterpret_cast<float (*)(float)>(&cosf));
    c.call(c.rax);
    c.movss(result, c.xmm0);
  } else {
    c.movsd(c.xmm0, a);
    c.mov(c.rax, (uint64_t) reinterpret_cast<double (*)(double)>(&cos));
    c.call(c.rax);
    c.movsd(result, c.xmm0);
  }

  e.RestoreArgs();
}

EMITTER(AND) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    c.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.and (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.and (result, b);
  }
}

EMITTER(OR) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    c.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.or (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.or (result, b);
  }
}

EMITTER(XOR) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    c.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.xor (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.xor (result, b);
  }
}

EMITTER(NOT) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    c.mov(result, a);
  }

  c.not(result);
}

EMITTER(SHL) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    c.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.shl(result, (int)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.mov(c.cl, b);
    c.shl(result, c.cl);

#ifdef PLATFORM_WINDOWS
    // arg0 was in rcx, needs to be restored
    e.RestoreArg0();
#endif
  }
}

EMITTER(ASHR) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    c.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.sar(result, (int)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.mov(c.cl, b);
    c.sar(result, c.cl);

#ifdef PLATFORM_WINDOWS
    // arg0 was in rcx, need to be restored
    e.RestoreArg0();
#endif
  }
}

EMITTER(LSHR) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    c.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.shr(result, (int)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    c.mov(c.cl, b);
    c.shr(result, c.cl);

#ifdef PLATFORM_WINDOWS
    // arg0 was in rcx, need to be restored
    e.RestoreArg0();
#endif
  }
}

EMITTER(BRANCH) {
  if (instr->arg0()->type() == VALUE_BLOCK) {
    // jump to local block
    const Block *dst = instr->arg0()->value<const Block *>();
    c.jmp(GetLabel(dst), Xbyak::CodeGenerator::T_NEAR);
  } else {
    // return if we need to branch to a far block
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());
    c.mov(c.rax, a);
    c.jmp(e.epilog_label());
  }
}

EMITTER(BRANCH_COND) {
  const Xbyak::Reg &cond = e.GetRegister(instr->arg0());

  c.test(cond, cond);

  // if both blocks are a local block this is easy
  if (instr->arg1()->type() == VALUE_BLOCK &&
      instr->arg2()->type() == VALUE_BLOCK) {
    // jump to local block
    const Block *next_block = instr->block()->next();
    const Block *block_true = instr->arg1()->value<const Block *>();
    const Block *block_false = instr->arg2()->value<const Block *>();

    // don't emit a jump if the block is next
    if (next_block != block_true) {
      c.jnz(GetLabel(block_true), Xbyak::CodeGenerator::T_NEAR);
    }
    if (next_block != block_false) {
      c.je(GetLabel(block_false), Xbyak::CodeGenerator::T_NEAR);
    }
  }
  // if both blocks are a far block this is easy
  else if (instr->arg1()->type() != VALUE_BLOCK &&
           instr->arg2()->type() != VALUE_BLOCK) {
    // return if we need to branch to a far block
    const Xbyak::Reg &op_true = e.GetRegister(instr->arg1());
    c.mov(c.eax, op_true);
    c.jnz(e.epilog_label());

    const Xbyak::Reg &op_false = e.GetRegister(instr->arg2());
    c.mov(c.eax, op_false);
    c.je(e.epilog_label());
  }
  // if they are mixed, do local block test first, far block second
  else {
    LOG_FATAL("Unexpected mixed mode conditional branch");
  }
}

EMITTER(CALL_EXTERNAL) {
  // rdi is already pointing to guest_ctx
  uint64_t addr = instr->arg0()->GetZExtValue();
  c.mov(c.rax, addr);
  c.call(c.rax);

  e.RestoreArgs();
}
