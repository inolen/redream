#include "core/core.h"
#include "emu/profiler.h"
#include "jit/backend/x64/x64_backend.h"
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

// map register ids coming from IR values for callee saved registers. use
// nullptr to specify that the register isn't saved
static const Xbyak::Reg *callee_save_map[] = {
    &Xbyak::util::rbx, &Xbyak::util::rbp, &Xbyak::util::r12, &Xbyak::util::r13,
    &Xbyak::util::r14, &Xbyak::util::r15, nullptr,           nullptr,
    nullptr,           nullptr,           nullptr,           nullptr,
    nullptr,           nullptr};

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
typedef void (*X64Emit)(X64Emitter &, Memory &, const Instr *);

static X64Emit x64_emitters[NUM_OPCODES];

#define EMITTER(op)                                           \
  void op(X64Emitter &e, Memory &memory, const Instr *instr); \
  static struct _x64_##op##_init {                            \
    _x64_##op##_init() { x64_emitters[OP_##op] = &op; }       \
  } x64_##op##_init;                                          \
  void op(X64Emitter &e, Memory &memory, const Instr *instr)

X64Emitter::X64Emitter(Memory &memory, size_t max_size)
    : CodeGenerator(max_size), memory_(memory), arena_(1024) {
  modified_marker_ = 0;
  modified_ = new int[x64_num_registers];

  Reset();
}

X64Emitter::~X64Emitter() { delete[] modified_; }

void X64Emitter::Reset() {
  reset();

  modified_marker_ = 0;
  memset(modified_, modified_marker_, sizeof(int) * x64_num_registers);
}

X64Fn X64Emitter::Emit(IRBuilder &builder) {
  PROFILER_RUNTIME("X64Emitter::Emit");

  // getCurr returns the current spot in the codegen buffer which the function
  // is about to emitted to
  X64Fn fn = getCurr<X64Fn>();

  // reset emit state
  arena_.Reset();
  epilog_label_ = AllocLabel();

  int stack_size = 0;
  EmitProlog(builder, &stack_size);
  EmitBody(builder);
  EmitEpilog(builder, stack_size);
  ready();

  return fn;
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

void X64Emitter::EmitProlog(IRBuilder &builder, int *out_stack_size) {
  int stack_size = STACK_SIZE;

  // align locals
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
  CHECK_EQ((stack_size + 8) % 16, 0);

  // mark which registers have been modified
  modified_marker_++;

  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      Value *result = instr->result();
      if (!result) {
        continue;
      }

      int i = result->reg();
      if (i == NO_REGISTER) {
        continue;
      }

      modified_[i] = modified_marker_;
    }
  }

  // push the callee-saved registers which have been modified
  int pushed = 0;

  for (int i = 0; i < x64_num_registers; i++) {
    const Xbyak::Reg *reg = callee_save_map[i];
    if (!reg) {
      continue;
    }

    if (modified_[i] == modified_marker_) {
      push(*reg);
      pushed++;
    }
  }

  // if an odd amount of push instructions are emitted stack_size needs to be
  // adjusted to keep the stack aligned
  if ((pushed % 2) == 1) {
    stack_size += 8;
  }

  // adjust stack pointer
  sub(Xbyak::util::rsp, stack_size);

  // save off arguments to stack in case they need to be restored
  mov(qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT], int_arg0);
  mov(qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY], int_arg1);

  *out_stack_size = stack_size;
}

void X64Emitter::EmitBody(IRBuilder &builder) {
  // generate labels for each block
  for (auto block : builder.blocks()) {
    Xbyak::Label *lbl = AllocLabel();
    SetLabel(block, lbl);
  }

  // emit each instruction
  for (auto block : builder.blocks()) {
    L(GetLabel(block));

    for (auto instr : block->instrs()) {
      X64Emit emit = x64_emitters[instr->op()];
      CHECK(emit, "Failed to find emitter for %s", Opnames[instr->op()]);
      emit(*this, memory_, instr);
    }
  }
}

void X64Emitter::EmitEpilog(IRBuilder &builder, int stack_size) {
  L(epilog_label());

  // adjust stack pointer
  add(Xbyak::util::rsp, stack_size);

  // pop callee-saved registers which have been modified
  for (int i = x64_num_registers - 1; i >= 0; i--) {
    const Xbyak::Reg *reg = callee_save_map[i];
    if (!reg) {
      continue;
    }

    if (modified_[i] == modified_marker_) {
      pop(*reg);
    }
  }

  ret();
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
        tmp = &r9b;
        break;
      case VALUE_I16:
        tmp = &r9w;
        break;
      case VALUE_I32:
        tmp = &r9d;
        break;
      case VALUE_I64:
        tmp = &r9;
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
    CopyOperand(v, xmm1);
    return xmm1;
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
        mov(eax, *reinterpret_cast<int32_t *>(&val));
        movd(reinterpret_cast<const Xbyak::Xmm &>(to), eax);
      } else {
        double val = v->value<double>();
        mov(rax, *reinterpret_cast<int64_t *>(&val));
        movq(reinterpret_cast<const Xbyak::Xmm &>(to), rax);
      }
    } else {
      CHECK(IsIntType(v->type()));

      mov(to, v->GetZExtValue());
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
        movdqa(reinterpret_cast<const Xbyak::Xmm &>(to), from);
      } else if (from.isBit(32)) {
        movss(reinterpret_cast<const Xbyak::Xmm &>(to), from);
      } else if (from.isBit(64)) {
        movsd(reinterpret_cast<const Xbyak::Xmm &>(to), from);
      } else {
        LOG_FATAL("Unexpected copy");
      }
    } else if (from.isXMM()) {
      CHECK(to.isMEM(), "Expected destination to be a memory address");

      if (to.isBit(32)) {
        movss(reinterpret_cast<const Xbyak::Address &>(to),
              reinterpret_cast<const Xbyak::Xmm &>(from));
      } else if (to.isBit(64)) {
        movsd(reinterpret_cast<const Xbyak::Address &>(to),
              reinterpret_cast<const Xbyak::Xmm &>(from));
      } else {
        LOG_FATAL("Unexpected copy");
      }
    } else {
      mov(to, from);
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
  mov(int_arg0, qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT]);
}

void X64Emitter::RestoreArg1() {
  mov(int_arg1, qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY]);
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
        e.movss(result, e.dword[int_arg0 + offset]);
        break;
      case VALUE_F64:
        e.movsd(result, e.qword[int_arg0 + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());

    switch (instr->result()->type()) {
      case VALUE_I8:
        e.mov(result, e.byte[int_arg0 + offset]);
        break;
      case VALUE_I16:
        e.mov(result, e.word[int_arg0 + offset]);
        break;
      case VALUE_I32:
        e.mov(result, e.dword[int_arg0 + offset]);
        break;
      case VALUE_I64:
        e.mov(result, e.qword[int_arg0 + offset]);
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
        e.mov(e.byte[int_arg0 + offset], instr->arg1()->value<int8_t>());
        break;
      case VALUE_I16:
        e.mov(e.word[int_arg0 + offset], instr->arg1()->value<int16_t>());
        break;
      case VALUE_I32:
      case VALUE_F32:
        e.mov(e.dword[int_arg0 + offset], instr->arg1()->value<int32_t>());
        break;
      case VALUE_I64:
      case VALUE_F64:
        e.mov(e.qword[int_arg0 + offset], instr->arg1()->value<int64_t>());
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
          e.movss(e.dword[int_arg0 + offset], src);
          break;
        case VALUE_F64:
          e.movsd(e.qword[int_arg0 + offset], src);
          break;
        default:
          LOG_FATAL("Unexpected value type");
          break;
      }
    } else {
      const Xbyak::Reg &src = e.GetRegister(instr->arg1());

      switch (instr->arg1()->type()) {
        case VALUE_I8:
          e.mov(e.byte[int_arg0 + offset], src);
          break;
        case VALUE_I16:
          e.mov(e.word[int_arg0 + offset], src);
          break;
        case VALUE_I32:
          e.mov(e.dword[int_arg0 + offset], src);
          break;
        case VALUE_I64:
          e.mov(e.qword[int_arg0 + offset], src);
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
        e.movss(result, e.dword[e.rsp + offset]);
        break;
      case VALUE_F64:
        e.movsd(result, e.qword[e.rsp + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());

    switch (instr->result()->type()) {
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
  int offset = instr->arg0()->value<int32_t>();

  CHECK(!instr->arg1()->constant());

  if (IsFloatType(instr->arg1()->type())) {
    const Xbyak::Xmm &src = e.GetXMMRegister(instr->arg1());

    switch (instr->arg1()->type()) {
      case VALUE_F32:
        e.movss(e.dword[e.rsp + offset], src);
        break;
      case VALUE_F64:
        e.movsd(e.qword[e.rsp + offset], src);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    const Xbyak::Reg &src = e.GetRegister(instr->arg1());

    switch (instr->arg1()->type()) {
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
      e.mov(e.rax, (size_t)physical_addr);

      switch (instr->result()->type()) {
        case VALUE_I8:
          e.mov(result, e.byte[e.rax]);
          break;
        case VALUE_I16:
          e.mov(result, e.word[e.rax]);
          break;
        case VALUE_I32:
          e.mov(result, e.dword[e.rax]);
          break;
        case VALUE_I64:
          e.mov(result, e.qword[e.rax]);
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
  e.mov(int_arg0, int_arg1);
  e.mov(int_arg1, a);

  // call func
  e.mov(e.rax, (uintptr_t)fn);
  e.call(e.rax);

  // copy off result
  e.mov(result, e.rax);

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
      e.mov(e.rax, (size_t)physical_addr);

      switch (instr->arg1()->type()) {
        case VALUE_I8:
          e.mov(e.byte[e.rax], b);
          break;
        case VALUE_I16:
          e.mov(e.word[e.rax], b);
          break;
        case VALUE_I32:
          e.mov(e.dword[e.rax], b);
          break;
        case VALUE_I64:
          e.mov(e.qword[e.rax], b);
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
  e.mov(int_arg0, int_arg1);
  e.mov(int_arg1, a);
  e.mov(int_arg2, b);

  // call func
  e.mov(e.rax, (uintptr_t)fn);
  e.call(e.rax);

  e.RestoreArgs();
}

EMITTER(CAST) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    switch (instr->result()->type()) {
      case VALUE_F32:
        CHECK_EQ(instr->arg0()->type(), VALUE_I32);
        e.cvtsi2ss(result, a);
        break;
      case VALUE_F64:
        CHECK_EQ(instr->arg0()->type(), VALUE_I64);
        e.cvtsi2sd(result, a);
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
        e.cvttss2si(result, a);
        break;
      case VALUE_I64:
        CHECK_EQ(instr->arg0()->type(), VALUE_F64);
        e.cvttsd2si(result, a);
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
    e.movsxd(result.cvt64(), a);
  } else {
    e.movsx(result, a);
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
    e.mov(result.cvt32(), a);
  } else {
    e.movzx(result, a);
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
    e.mov(result, truncated);
  } else {
    e.movzx(result.cvt32(), truncated);
  }
}

EMITTER(SELECT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &cond = e.GetRegister(instr->arg0());
  const Xbyak::Reg &a = e.GetRegister(instr->arg1());
  const Xbyak::Reg &b = e.GetRegister(instr->arg2());

  e.test(cond, cond);
  e.cmovnz(result.cvt32(), a);
  e.cmovz(result.cvt32(), b);
}

EMITTER(EQ) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }
  }

  e.sete(result);
}

EMITTER(NE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }
  }

  e.setne(result);
}

EMITTER(SGE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }

    e.setae(result);
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }

    e.setge(result);
  }
}

EMITTER(SGT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }

    e.seta(result);
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }

    e.setg(result);
  }
}

EMITTER(UGE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.cmp(a, b);
  }

  e.setae(result);
}

EMITTER(UGT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.cmp(a, b);
  }

  e.seta(result);
}

EMITTER(SLE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }

    e.setbe(result);
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }

    e.setle(result);
  }
}

EMITTER(SLT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }

    e.setb(result);
  } else {
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }

    e.setl(result);
  }
}

EMITTER(ULE) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.cmp(a, b);
  }

  e.setbe(result);
}

EMITTER(ULT) {
  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.cmp(a, b);
  }

  e.setb(result);
}

EMITTER(ADD) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

    if (instr->result()->type() == VALUE_F32) {
      if (result != a) {
        e.movss(result, a);
      }

      e.addss(result, b);
    } else {
      if (result != a) {
        e.movsd(result, a);
      }

      e.addsd(result, b);
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (result != a) {
      e.mov(result, a);
    }

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.add(result, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      e.add(result, b);
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
        e.movss(result, a);
      }

      e.subss(result, b);
    } else {
      if (result != a) {
        e.movsd(result, a);
      }

      e.subsd(result, b);
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (result != a) {
      e.mov(result, a);
    }

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.sub(result, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg &b = e.GetRegister(instr->arg1());
      e.sub(result, b);
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
        e.movss(result, a);
      }

      e.mulss(result, b);
    } else {
      if (result != a) {
        e.movsd(result, a);
      }

      e.mulsd(result, b);
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());

    if (result != a) {
      e.mov(result, a);
    }

    e.imul(result, b);
  }
}

EMITTER(UMUL) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());
  const Xbyak::Reg &b = e.GetRegister(instr->arg1());

  if (result != a) {
    e.mov(result, a);
  }

  e.imul(result, b);
}

EMITTER(DIV) {
  CHECK(IsFloatType(instr->result()->type()));

  const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
  const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());
  const Xbyak::Xmm &b = e.GetXMMRegister(instr->arg1());

  if (instr->result()->type() == VALUE_F32) {
    if (result != a) {
      e.movss(result, a);
    }

    e.divss(result, b);
  } else {
    if (result != a) {
      e.movsd(result, a);
    }

    e.divsd(result, b);
  }
}

EMITTER(NEG) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

    if (instr->result()->type() == VALUE_F32) {
      // TODO use xorps
      e.movd(e.eax, a);
      e.xor (e.eax, (uint32_t)0x80000000);
      e.movd(result, e.eax);
    } else {
      // TODO use xorpd
      e.movq(e.rax, a);
      e.mov(e.r9, (uint64_t)0x8000000000000000);
      e.xor (e.rax, e.r9);
      e.movq(result, e.rax);
    }
  } else {
    const Xbyak::Reg &result = e.GetRegister(instr->result());
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());

    if (result != a) {
      e.mov(result, a);
    }

    e.neg(result);
  }
}

EMITTER(SQRT) {
  CHECK(IsFloatType(instr->result()->type()));

  const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
  const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

  if (instr->result()->type() == VALUE_F32) {
    e.sqrtss(result, a);
  } else {
    e.sqrtsd(result, a);
  }
}

EMITTER(ABS) {
  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

    if (instr->result()->type() == VALUE_F32) {
      // TODO use andps
      e.movd(e.eax, a);
      e.and (e.eax, (uint32_t)0x7fffffff);
      e.movd(result, e.eax);
    } else {
      // TODO use andpd
      e.movq(e.rax, a);
      e.mov(e.r9, (uint64_t)0x7fffffffffffffff);
      e.and (e.rax, e.r9);
      e.movq(result, e.rax);
    }
  } else {
    LOG_FATAL("Unexpected abs result type");
    // e.mov(e.rax, *result);
    // e.neg(e.rax);
    // e.cmovl(reinterpret_cast<const Xbyak::Reg *>(result)->cvt32(), e.rax);
  }
}

EMITTER(SIN) {
  CHECK(IsFloatType(instr->result()->type()));

  const Xbyak::Xmm &result = e.GetXMMRegister(instr->result());
  const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0());

  // FIXME xmm registers aren't callee saved, this would probably break if we
  // used the lower indexed xmm registers
  if (instr->result()->type() == VALUE_F32) {
    e.movss(e.xmm0, a);
    e.mov(e.rax, (uint64_t) reinterpret_cast<float (*)(float)>(&sinf));
    e.call(e.rax);
    e.movss(result, e.xmm0);
  } else {
    e.movsd(e.xmm0, a);
    e.mov(e.rax, (uint64_t) reinterpret_cast<double (*)(double)>(&sin));
    e.call(e.rax);
    e.movsd(result, e.xmm0);
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
    e.movss(e.xmm0, a);
    e.mov(e.rax, (uint64_t) reinterpret_cast<float (*)(float)>(&cosf));
    e.call(e.rax);
    e.movss(result, e.xmm0);
  } else {
    e.movsd(e.xmm0, a);
    e.mov(e.rax, (uint64_t) reinterpret_cast<double (*)(double)>(&cos));
    e.call(e.rax);
    e.movsd(result, e.xmm0);
  }

  e.RestoreArgs();
}

EMITTER(AND) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.and (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.and (result, b);
  }
}

EMITTER(OR) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.or (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.or (result, b);
  }
}

EMITTER(XOR) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.xor (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.xor (result, b);
  }
}

EMITTER(NOT) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  e.not(result);
}

EMITTER(SHL) {
  CHECK(IsIntType(instr->result()->type()));

  const Xbyak::Reg &result = e.GetRegister(instr->result());
  const Xbyak::Reg &a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.shl(result, (int)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.mov(e.cl, b);
    e.shl(result, e.cl);

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
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.sar(result, (int)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.mov(e.cl, b);
    e.sar(result, e.cl);

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
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.shr(result, (int)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg &b = e.GetRegister(instr->arg1());
    e.mov(e.cl, b);
    e.shr(result, e.cl);

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
    e.jmp(GetLabel(dst), Xbyak::CodeGenerator::T_NEAR);
  } else {
    // return if we need to branch to a far block
    const Xbyak::Reg &a = e.GetRegister(instr->arg0());
    e.mov(e.rax, a);
    e.jmp(e.epilog_label());
  }
}

EMITTER(BRANCH_COND) {
  const Xbyak::Reg &cond = e.GetRegister(instr->arg0());

  e.test(cond, cond);

  // if both blocks are a local block this is easy
  if (instr->arg1()->type() == VALUE_BLOCK &&
      instr->arg2()->type() == VALUE_BLOCK) {
    // jump to local block
    const Block *next_block = instr->block()->next();
    const Block *block_true = instr->arg1()->value<const Block *>();
    const Block *block_false = instr->arg2()->value<const Block *>();

    // don't emit a jump if the block is next
    if (next_block != block_true) {
      e.jnz(GetLabel(block_true), Xbyak::CodeGenerator::T_NEAR);
    }
    if (next_block != block_false) {
      e.je(GetLabel(block_false), Xbyak::CodeGenerator::T_NEAR);
    }
  }
  // if both blocks are a far block this is easy
  else if (instr->arg1()->type() != VALUE_BLOCK &&
           instr->arg2()->type() != VALUE_BLOCK) {
    // return if we need to branch to a far block
    const Xbyak::Reg &op_true = e.GetRegister(instr->arg1());
    e.mov(e.eax, op_true);
    e.jnz(e.epilog_label());

    const Xbyak::Reg &op_false = e.GetRegister(instr->arg2());
    e.mov(e.eax, op_false);
    e.je(e.epilog_label());
  }
  // if they are mixed, do local block test first, far block second
  else {
    LOG_FATAL("Unexpected mixed mode conditional branch");
  }
}

EMITTER(CALL_EXTERNAL) {
  // rdi is already pointing to guest_ctx
  e.CopyOperand(instr->arg0(), e.rax);
  e.call(e.rax);

  e.RestoreArgs();
}
