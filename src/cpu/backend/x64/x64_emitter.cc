#include "cpu/backend/x64/x64_emitter.h"
#include "emu/memory.h"

using namespace dreavm::core;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

static const Xbyak::Reg *reg_map_8[] = {&Xbyak::util::bl,
                                        &Xbyak::util::bpl,
                                        &Xbyak::util::r12b,
                                        &Xbyak::util::r13b,
                                        &Xbyak::util::r14b,
                                        &Xbyak::util::r15b,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr};

static const Xbyak::Reg *reg_map_16[] = {&Xbyak::util::bx,
                                         &Xbyak::util::bp,
                                         &Xbyak::util::r12w,
                                         &Xbyak::util::r13w,
                                         &Xbyak::util::r14w,
                                         &Xbyak::util::r15w,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         nullptr};

static const Xbyak::Reg *reg_map_32[] = {
    &Xbyak::util::ebx,  &Xbyak::util::ebp,  &Xbyak::util::r12d,
    &Xbyak::util::r13d, &Xbyak::util::r14d, &Xbyak::util::r15d,
    &Xbyak::util::xmm2, &Xbyak::util::xmm3, &Xbyak::util::xmm4,
    &Xbyak::util::xmm5, &Xbyak::util::xmm6, &Xbyak::util::xmm7};

static const Xbyak::Reg *reg_map_64[] = {
    &Xbyak::util::rbx,  &Xbyak::util::rbp,  &Xbyak::util::r12,
    &Xbyak::util::r13,  &Xbyak::util::r14,  &Xbyak::util::r15,
    &Xbyak::util::xmm2, &Xbyak::util::xmm3, &Xbyak::util::xmm4,
    &Xbyak::util::xmm5, &Xbyak::util::xmm6, &Xbyak::util::xmm7};

// callbacks for emitting each IR op
typedef void (*X64Emit)(X64Emitter &, Xbyak::CodeGenerator &, const Instr *);

static X64Emit x64_emitters[NUM_OPCODES];

#define EMITTER(op)                                                    \
  void op(X64Emitter &e, Xbyak::CodeGenerator &c, const Instr *instr); \
  static struct _x64_##op##_init {                                     \
    _x64_##op##_init() { x64_emitters[OP_##op] = &op; }                \
  } x64_##op##_init;                                                   \
  void op(X64Emitter &e, Xbyak::CodeGenerator &c, const Instr *instr)

X64Emitter::X64Emitter(Xbyak::CodeGenerator &codegen)
    : c_(codegen), operand_arena_(1024) {}

X64Fn X64Emitter::Emit(IRBuilder &builder) {
  // getCurr returns the current spot in the codegen buffer which the function
  // is about to emitted to
  X64Fn fn = c_.getCurr<X64Fn>();

  // reset arena holding temporary operands used during emitting
  operand_arena_.Reset();

  // stack must be 16 byte aligned
  // TODO align each local
  int stack_size = 16 + builder.locals_size();
  // add 8 for function return value which will be pushed when this is called
  stack_size = align(stack_size, 16) + 8;
  assert((stack_size + 8) % 16 == 0);

  c_.inLocalLabel();

  // emit prolog
  // FIXME only push registers that're used
  c_.push(Xbyak::util::rbx);
  c_.push(Xbyak::util::rbp);
  c_.push(Xbyak::util::r12);
  c_.push(Xbyak::util::r13);
  c_.push(Xbyak::util::r14);
  c_.push(Xbyak::util::r15);

  // reserve stack space for rdi copy
  c_.sub(Xbyak::util::rsp, stack_size);
  c_.mov(c_.qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT],
         Xbyak::util::rdi);
  c_.mov(c_.qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY], Xbyak::util::rsi);

  // assign ordinals for each block
  int ordinal = 0;
  for (auto block : builder.blocks()) {
    block->set_tag(ordinal++);
  }

  for (auto block : builder.blocks()) {
    // generate label for this ordinal
    c_.L("." + std::to_string((int)block->tag()));

    for (auto instr : block->instrs()) {
      X64Emit emit = x64_emitters[instr->op()];
      CHECK(emit) << "Failed to find emitter for " << Opnames[instr->op()];
      emit(*this, c_, instr);
    }
  }

  // emit prolog
  c_.L(".epilog");

  // reset stack
  c_.add(Xbyak::util::rsp, stack_size);

  // TODO only pop registers that're used
  c_.pop(Xbyak::util::r15);
  c_.pop(Xbyak::util::r14);
  c_.pop(Xbyak::util::r13);
  c_.pop(Xbyak::util::r12);
  c_.pop(Xbyak::util::rbp);
  c_.pop(Xbyak::util::rbx);

  c_.ret();

  c_.outLocalLabel();
  c_.align(16);

  // patch up relocations
  c_.ready();

  // return the start of the buffer
  return fn;
}

// Get the register / local allocated for the supplied value. The size argument
// can be overridden to get a truncated version of the value.
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
  } else if (v->local() != NO_SLOT) {
    Xbyak::Address *addr = operand_arena_.Alloc<Xbyak::Address>();

    int offset = STACK_OFFSET_LOCALS + v->local();

    switch (size) {
      case 8:
        *addr = c_.qword[Xbyak::util::rsp + offset];
        break;
      case 4:
        *addr = c_.dword[Xbyak::util::rsp + offset];
        break;
      case 2:
        *addr = c_.word[Xbyak::util::rsp + offset];
        break;
      case 1:
        *addr = c_.byte[Xbyak::util::rsp + offset];
        break;
    }

    CHECK_NOTNULL(addr);

    return *addr;
  }

  LOG(FATAL) << "Value was not allocated a register or local";
}

// If the value is a constant, copy it to the temporary operand, else return
// the local or register allocated for it.
const Xbyak::Operand &X64Emitter::GetOperand(const Value *v,
                                             const Xbyak::Operand &tmp) {
  if (v->reg() == NO_REGISTER && v->local() == NO_SLOT) {
    // copy constant to tmp
    CopyOperand(v, tmp);
    return tmp;
  }

  return GetOperand(v);
}

// If the value is a local or constant, copy it to the tempory register, else
// return the register allocated for it.
const Xbyak::Reg &X64Emitter::GetRegister(const Value *v,
                                          const Xbyak::Reg &tmp) {
  if (v->reg() == NO_REGISTER) {
    // copy local / constant to mp
    CopyOperand(v, tmp);
    return tmp;
  }

  return reinterpret_cast<const Xbyak::Reg &>(GetOperand(v));
}

// If the value isn't allocated a XMM register copy it to the temporary XMM,
// register, else return the XMM register allocated for it.
const Xbyak::Xmm &X64Emitter::GetXMMRegister(const Value *v,
                                             const Xbyak::Xmm &tmp) {
  const Xbyak::Operand &op = GetOperand(v);

  if (!op.isXMM()) {
    CopyOperand(v, tmp);
    return tmp;
  }

  return reinterpret_cast<const Xbyak::Xmm &>(op);
}

// If the prefered operand is an XMM register, copy the value to it and return,
// else do the regular GetXMMRegister lookup.
const Xbyak::Xmm &X64Emitter::GetXMMRegister(const Value *v,
                                             const Xbyak::Operand &prefered,
                                             const Xbyak::Xmm &tmp) {
  if (prefered.isXMM()) {
    CopyOperand(v, prefered);
    return reinterpret_cast<const Xbyak::Xmm &>(prefered);
  }

  return GetXMMRegister(v, tmp);
}

// Copy the value from src to dst if they're not the same operand.
// TODO when copying XMM registers during SIN / COS a movdqa isn't actually
// necessary (could pass in size info to perform movss / movsd). bummer that
// there isn't xmm0d, etc.
const Xbyak::Operand &X64Emitter::CopyOperand(const Xbyak::Operand &from,
                                              const Xbyak::Operand &to) {
  if (from == to) {
    return to;
  }

  if (to.isXMM()) {
    if (from.isXMM()) {
      c_.movdqa(reinterpret_cast<const Xbyak::Xmm &>(to), from);
    } else if (from.isBit(32)) {
      c_.movss(reinterpret_cast<const Xbyak::Xmm &>(to), from);
    } else if (from.isBit(64)) {
      c_.movsd(reinterpret_cast<const Xbyak::Xmm &>(to), from);
    } else {
      LOG(FATAL) << "Unsupported copy";
    }
  } else if (from.isXMM()) {
    CHECK(to.isMEM()) << "Expected destination to be a memory address";

    if (to.isBit(32)) {
      c_.movss(reinterpret_cast<const Xbyak::Address &>(to),
               reinterpret_cast<const Xbyak::Xmm &>(from));
    } else if (to.isBit(64)) {
      c_.movsd(reinterpret_cast<const Xbyak::Address &>(to),
               reinterpret_cast<const Xbyak::Xmm &>(from));
    } else {
      LOG(FATAL) << "Unsupported copy";
    }
  } else {
    c_.mov(to, from);
  }

  return to;
}

// Copy the value to the supplied operand.
const Xbyak::Operand &X64Emitter::CopyOperand(const Value *v,
                                              const Xbyak::Operand &to) {
  if (v->constant()) {
    if (to.isXMM()) {
      if (v->type() == VALUE_F32) {
        float val = v->value<float>();
        c_.mov(c_.r8d, *reinterpret_cast<int32_t *>(&val));
        c_.movd(reinterpret_cast<const Xbyak::Xmm &>(to), c_.r8d);
      } else {
        double val = v->value<double>();
        c_.mov(c_.r8, *reinterpret_cast<int64_t *>(&val));
        c_.movq(reinterpret_cast<const Xbyak::Xmm &>(to), c_.r8);
      }
    } else {
      c_.mov(to, v->GetZExtValue());
    }
  } else {
    const Xbyak::Operand &from = GetOperand(v);

    CopyOperand(from, to);
  }

  return to;
}

bool X64Emitter::CanEncodeAsImmediate(const Value *v) {
  if (!v->constant()) {
    return false;
  }

  return v->type() <= VALUE_I32;
}

EMITTER(LOAD_CONTEXT) {
  int offset = instr->arg0()->value<int32_t>();
  int result_sz = SizeForType(instr->result()->type());
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (result.isXMM()) {
    const Xbyak::Xmm &result_xmm = reinterpret_cast<const Xbyak::Xmm &>(result);

    if (result_sz == 4) {
      c.movss(result_xmm, c.dword[c.rdi + offset]);
    } else if (result_sz == 8) {
      c.movsd(result_xmm, c.qword[c.rdi + offset]);
    }
  } else {
    // dst must be a register, mov doesn't support mem -> mem
    const Xbyak::Reg &tmp = e.GetRegister(instr->result(), c.rax);

    if (result_sz == 1) {
      c.mov(tmp, c.byte[c.rdi + offset]);
    } else if (result_sz == 2) {
      c.mov(tmp, c.word[c.rdi + offset]);
    } else if (result_sz == 4) {
      c.mov(tmp, c.dword[c.rdi + offset]);
    } else if (result_sz == 8) {
      c.mov(tmp, c.qword[c.rdi + offset]);
    }

    if (tmp != result) {
      c.mov(result, tmp);
    }
  }
}

EMITTER(STORE_CONTEXT) {
  int offset = instr->arg0()->value<int32_t>();
  int data_sz = SizeForType(instr->arg1()->type());

  if (instr->arg1()->constant()) {
    if (data_sz == 1) {
      c.mov(c.byte[c.rdi + offset], instr->arg1()->value<int8_t>());
    } else if (data_sz == 2) {
      c.mov(c.word[c.rdi + offset], instr->arg1()->value<int16_t>());
    } else if (data_sz == 4) {
      c.mov(c.dword[c.rdi + offset], instr->arg1()->value<int32_t>());
    } else if (data_sz == 8) {
      c.mov(c.qword[c.rdi + offset], instr->arg1()->value<int64_t>());
    }
  } else {
    const Xbyak::Operand &src = e.GetOperand(instr->arg1());

    if (src.isXMM()) {
      const Xbyak::Xmm &src_xmm = reinterpret_cast<const Xbyak::Xmm &>(src);

      if (data_sz == 4) {
        c.movss(c.dword[c.rdi + offset], src_xmm);
      } else if (data_sz == 8) {
        c.movsd(c.qword[c.rdi + offset], src_xmm);
      }
    } else {
      // src must come from a register mov doesn't support mem -> mem
      const Xbyak::Reg &src_reg = reinterpret_cast<const Xbyak::Reg &>(
          src.isREG() ? src : e.CopyOperand(src, c.rax));

      if (data_sz == 1) {
        c.mov(c.byte[c.rdi + offset], src_reg);
      } else if (data_sz == 2) {
        c.mov(c.word[c.rdi + offset], src_reg);
      } else if (data_sz == 4) {
        c.mov(c.dword[c.rdi + offset], src_reg);
      } else if (data_sz == 8) {
        c.mov(c.qword[c.rdi + offset], src_reg);
      }
    }
  }
}

uint8_t R8(Memory *memory, uint32_t addr) { return memory->R8(addr); }
uint16_t R16(Memory *memory, uint32_t addr) { return memory->R16(addr); }
uint32_t R32(Memory *memory, uint32_t addr) { return memory->R32(addr); }
uint64_t R64(Memory *memory, uint32_t addr) { return memory->R64(addr); }
EMITTER(LOAD) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  void *fn = nullptr;
  switch (instr->result()->type()) {
    case VALUE_I8:
      fn = reinterpret_cast<void *>(&R8);
      break;
    case VALUE_I16:
      fn = reinterpret_cast<void *>(&R16);
      break;
    case VALUE_I32:
      fn = reinterpret_cast<void *>(&R32);
      break;
    case VALUE_I64:
      fn = reinterpret_cast<void *>(&R64);
      break;
    default:
      CHECK(false);
      break;
  }

  // setup arguments
  c.mov(c.rdi, c.rsi);
  e.CopyOperand(instr->arg0(), c.rsi);

  // call func
  c.mov(c.rax, (uintptr_t)fn);
  c.call(c.rax);

  // copy off result
  c.mov(result, c.rax);

  // restore rdi / rsi
  c.mov(Xbyak::util::rdi,
        c.qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT]);
  c.mov(Xbyak::util::rsi, c.qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY]);
}

void W8(Memory *memory, uint32_t addr, uint8_t v) { memory->W8(addr, v); }
void W16(Memory *memory, uint32_t addr, uint16_t v) { memory->W16(addr, v); }
void W32(Memory *memory, uint32_t addr, uint32_t v) { memory->W32(addr, v); }
void W64(Memory *memory, uint32_t addr, uint64_t v) { memory->W64(addr, v); }
EMITTER(STORE) {
  void *fn = nullptr;
  switch (instr->arg1()->type()) {
    case VALUE_I8:
      fn = reinterpret_cast<void *>(&W8);
      break;
    case VALUE_I16:
      fn = reinterpret_cast<void *>(&W16);
      break;
    case VALUE_I32:
      fn = reinterpret_cast<void *>(&W32);
      break;
    case VALUE_I64:
      fn = reinterpret_cast<void *>(&W64);
      break;
    default:
      CHECK(false);
      break;
  }

  // setup arguments
  c.mov(c.rdi, c.rsi);
  e.CopyOperand(instr->arg0(), c.rsi);
  e.CopyOperand(instr->arg1(), c.rdx);

  // call func
  c.mov(c.rax, (uintptr_t)fn);
  c.call(c.rax);

  // restore rdi / rsi
  c.mov(Xbyak::util::rdi,
        c.qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT]);
  c.mov(Xbyak::util::rsi, c.qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY]);
}

EMITTER(CAST) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.rax);

  switch (instr->result()->type()) {
    case VALUE_I32:
      CHECK_EQ(instr->arg0()->type(), VALUE_F32);
      c.cvttss2si(result, a);
      break;
    case VALUE_I64:
      CHECK_EQ(instr->arg0()->type(), VALUE_F64);
      c.cvttsd2si(result, a);
      break;
    case VALUE_F32:
      CHECK_EQ(instr->arg0()->type(), VALUE_I32);
      c.cvtsi2ss(result, a);
      break;
    case VALUE_F64:
      CHECK_EQ(instr->arg0()->type(), VALUE_I64);
      c.cvtsi2sd(result, a);
      break;
    default:
      CHECK(false);
      break;
  }
}

EMITTER(SEXT) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg0());

  if (a == result) {
    // already the correct width
    return;
  }

  const Xbyak::Reg &tmp = e.GetRegister(instr->result(), c.rax);

  if (result.isBit(64) && a.isBit(32)) {
    c.movsxd(tmp.cvt64(), a);
  } else {
    c.movsx(tmp, a);
  }

  if (tmp != result) {
    c.mov(result, tmp);
  }
}

EMITTER(ZEXT) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg0());

  if (a == result) {
    // already the correct width
    return;
  }

  const Xbyak::Reg &tmp = e.GetRegister(instr->result(), c.rax);

  if (result.isBit(64)) {
    // mov will automatically zero fill the upper 32-bits
    c.mov(tmp.cvt32(), a);
  } else {
    c.movzx(tmp, a);
  }

  if (tmp != result) {
    c.mov(result, tmp);
  }
}

EMITTER(TRUNCATE) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg0());

  if (a == result) {
    // already the correct width
    return;
  }

  const Xbyak::Reg &tmp = e.GetRegister(instr->result(), c.rax);
  const Xbyak::Operand &truncated =
      e.GetOperand(instr->arg0(), result.getBit() >> 3);

  // TODO fixme tmp should be size appropriate, c.mov is unnecesary, only need
  // movzx once tmp is correct
  if (truncated.isBit(32)) {
    c.mov(result, truncated);
  } else {
    c.movzx(tmp.cvt32(), truncated);
    if (tmp != result) {
      c.mov(result, tmp);
    }
  }
}

EMITTER(SELECT) {
  const Xbyak::Reg &cond = e.GetRegister(instr->arg0(), c.rax);

  c.test(cond, cond);

  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg1(), c.rax);
  const Xbyak::Operand &b = e.GetOperand(instr->arg2(), c.rcx);
  const Xbyak::Reg &tmp = e.GetRegister(instr->result(), c.rdx);

  c.cmovnz(tmp.cvt32(), a);
  c.cmovz(tmp.cvt32(), b);

  if (tmp != result) {
    c.mov(result, tmp);
  }
}

EMITTER(EQ) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }
  } else {
    const Xbyak::Operand &a = e.GetOperand(instr->arg0(), result);

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);
      c.cmp(a, b);
    }
  }

  c.sete(result);
}

EMITTER(NE) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }
  } else {
    const Xbyak::Operand &a = e.GetOperand(instr->arg0(), result);

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);
      c.cmp(a, b);
    }
  }

  c.setne(result);
}

EMITTER(SGE) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.xmm1);

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }

    c.setae(result);
  } else {
    const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.rax);

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rcx);
      c.cmp(a, b);
    }

    c.setge(result);
  }
}

EMITTER(SGT) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.xmm1);

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }

    c.seta(result);
  } else {
    const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.rax);

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rcx);
      c.cmp(a, b);
    }

    c.setg(result);
  }
}

EMITTER(UGE) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.rax);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rcx);
    c.cmp(a, b);
  }

  c.setae(result);
}

EMITTER(UGT) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.rax);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rcx);
    c.cmp(a, b);
  }

  c.seta(result);
}

EMITTER(SLE) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.xmm1);

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }

    c.setbe(result);
  } else {
    const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.rax);

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rcx);
      c.cmp(a, b);
    }

    c.setle(result);
  }
}

EMITTER(SLT) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.xmm1);

    if (instr->arg0()->type() == VALUE_F32) {
      c.comiss(a, b);
    } else {
      c.comisd(a, b);
    }

    c.setb(result);
  } else {
    const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.rax);

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rcx);
      c.cmp(a, b);
    }

    c.setl(result);
  }
}

EMITTER(ULE) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.rax);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rcx);
    c.cmp(a, b);
  }

  c.setbe(result);
}

EMITTER(ULT) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.rax);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rcx);
    c.cmp(a, b);
  }

  c.setb(result);
}

EMITTER(ADD) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), result, c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.xmm1);

    if (instr->result()->type() == VALUE_F32) {
      c.addss(a, b);
    } else {
      c.addsd(a, b);
    }

    e.CopyOperand(a, result);
  } else {
    e.CopyOperand(instr->arg0(), result);

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.add(result, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);
      c.add(result, b);
    }
  }
}

EMITTER(SUB) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), result, c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.xmm1);

    if (instr->result()->type() == VALUE_F32) {
      c.subss(a, b);
    } else {
      c.subsd(a, b);
    }

    e.CopyOperand(a, result);
  } else {
    e.CopyOperand(instr->arg0(), result);

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      c.sub(result, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);
      c.sub(result, b);
    }
  }
}

EMITTER(SMUL) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), result, c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.xmm1);

    if (instr->result()->type() == VALUE_F32) {
      c.mulss(a, b);
    } else {
      c.mulsd(a, b);
    }

    e.CopyOperand(a, result);
  } else {
    const Xbyak::Reg &tmp = e.GetRegister(instr->result(), c.rax);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);

    c.imul(tmp, b);

    if (tmp != result) {
      c.mov(result, tmp);
    }
  }
}

EMITTER(UMUL) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Reg &tmp = e.GetRegister(instr->result(), c.rax);
  const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);

  c.imul(tmp, b);

  if (tmp != result) {
    c.mov(result, tmp);
  }
}

// TODO could optimize by having a sdiv / udiv. no need to sign extend the
// accumulation register for udiv.
EMITTER(DIV) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), result, c.xmm0);
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.xmm1);

    if (instr->result()->type() == VALUE_F32) {
      c.divss(a, b);
    } else {
      c.divsd(a, b);
    }

    e.CopyOperand(a, result);
  } else {
    e.CopyOperand(instr->arg0(), result);

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      switch (instr->result()->type()) {
        case VALUE_I8:
          c.mov(c.al, instr->arg1()->value<int8_t>());
          break;
        case VALUE_I16:
          c.mov(c.ax, instr->arg1()->value<int16_t>());
          c.cwd();
          break;
        case VALUE_I32:
          c.mov(c.eax, instr->arg1()->value<int32_t>());
          c.cdq();
          break;
        case VALUE_I64:
          c.mov(c.rax, instr->arg1()->value<int64_t>());
          c.cqo();
          break;
        default:
          LOG(FATAL) << "Unexpected result type";
          break;
      }
    } else {
      const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);

      switch (instr->result()->type()) {
        case VALUE_I8:
          c.mov(c.al, b);
          break;
        case VALUE_I16:
          c.mov(c.ax, b);
          c.cwd();
          break;
        case VALUE_I32:
          c.mov(c.eax, b);
          c.cdq();
          break;
        case VALUE_I64:
          c.mov(c.rax, b);
          c.cqo();
          break;
        default:
          LOG(FATAL) << "Unexpected result type";
          break;
      }
    }

    c.idiv(result);
  }
}

EMITTER(NEG) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), result, c.xmm0);

    if (instr->result()->type() == VALUE_F32) {
      // TODO use xorps
      c.movd(c.eax, a);
      c.mov(c.ecx, (uint32_t)0x80000000);
      c.xor (c.eax, c.ecx);
      if (result.isXMM()) {
        c.movd(reinterpret_cast<const Xbyak::Xmm &>(result), c.eax);
      } else {
        c.mov(result, c.eax);
      }
    } else {
      // TODO use xorpd
      c.movq(c.rax, a);
      c.mov(c.rcx, (uint64_t)0x8000000000000000);
      c.xor (c.rax, c.rcx);
      if (result.isXMM()) {
        c.movq(reinterpret_cast<const Xbyak::Xmm &>(result), c.rax);
      } else {
        c.mov(result, c.rax);
      }
    }
  } else {
    e.CopyOperand(instr->arg0(), result);

    c.neg(result);
  }
}

EMITTER(SQRT) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());
  const Xbyak::Operand &a = e.GetOperand(instr->arg0(), c.xmm0);

  const Xbyak::Xmm &tmp =
      result.isXMM() ? reinterpret_cast<const Xbyak::Xmm &>(result) : c.xmm1;

  if (instr->result()->type() == VALUE_F32) {
    c.sqrtss(tmp, a);
  } else {
    c.sqrtsd(tmp, a);
  }

  e.CopyOperand(tmp, result);
}

EMITTER(ABS) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (IsFloatType(instr->result()->type())) {
    const Xbyak::Xmm &a = e.GetXMMRegister(instr->arg0(), result, c.xmm0);

    if (instr->result()->type() == VALUE_F32) {
      // TODO use andps
      c.movd(c.eax, a);
      c.mov(c.ecx, (uint32_t)0x7fffffff);
      c.and (c.eax, c.ecx);
      if (result.isXMM()) {
        c.movd(reinterpret_cast<const Xbyak::Xmm &>(result), c.eax);
      } else {
        c.mov(result, c.eax);
      }
    } else {
      // TODO use andpd
      c.movq(c.rax, a);
      c.mov(c.rcx, (uint64_t)0x7fffffffffffffff);
      c.and (c.rax, c.rcx);
      if (result.isXMM()) {
        c.movq(reinterpret_cast<const Xbyak::Xmm &>(result), c.rax);
      } else {
        c.mov(result, c.rax);
      }
    }
  } else {
    LOG(FATAL) << "Verify this works";
    // c.mov(c.rax, *result);
    // c.neg(c.rax);
    // c.cmovl(reinterpret_cast<const Xbyak::Reg *>(result)->cvt32(), c.rax);
  }
}

EMITTER(SIN) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (instr->result()->type() == VALUE_F32) {
    e.CopyOperand(instr->arg0(), c.xmm0);
    c.mov(c.rax, (uint64_t)&sinf);
    c.call(c.rax);
    if (result.isXMM()) {
      c.movss(reinterpret_cast<const Xbyak::Xmm &>(result), c.xmm0);
    } else {
      c.movss(reinterpret_cast<const Xbyak::Address &>(result), c.xmm0);
    }
  } else {
    e.CopyOperand(instr->arg0(), c.xmm0);
    c.mov(c.rax, (uint64_t)&sin);
    c.call(c.rax);
    if (result.isXMM()) {
      c.movsd(reinterpret_cast<const Xbyak::Xmm &>(result), c.xmm0);
    } else {
      c.movsd(reinterpret_cast<const Xbyak::Address &>(result), c.xmm0);
    }
  }

  // restore rdi / rsi
  c.mov(Xbyak::util::rdi,
        c.qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT]);
  c.mov(Xbyak::util::rsi, c.qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY]);
}

EMITTER(COS) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  if (instr->result()->type() == VALUE_F32) {
    e.CopyOperand(instr->arg0(), c.xmm0);
    c.mov(c.rax, (uint64_t)&cosf);
    c.call(c.rax);
    if (result.isXMM()) {
      c.movss(reinterpret_cast<const Xbyak::Xmm &>(result), c.xmm0);
    } else {
      c.movss(reinterpret_cast<const Xbyak::Address &>(result), c.xmm0);
    }
  } else {
    e.CopyOperand(instr->arg0(), c.xmm0);
    c.mov(c.rax, (uint64_t)&cos);
    c.call(c.rax);
    if (result.isXMM()) {
      c.movsd(reinterpret_cast<const Xbyak::Xmm &>(result), c.xmm0);
    } else {
      c.movsd(reinterpret_cast<const Xbyak::Address &>(result), c.xmm0);
    }
  }

  // restore rdi / rsi
  c.mov(Xbyak::util::rdi,
        c.qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT]);
  c.mov(Xbyak::util::rsi, c.qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY]);
}

EMITTER(AND) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  e.CopyOperand(instr->arg0(), result);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.and (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);
    c.and (result, b);
  }
}

EMITTER(OR) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  e.CopyOperand(instr->arg0(), result);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.or (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);
    c.or (result, b);
  }
}

EMITTER(XOR) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  e.CopyOperand(instr->arg0(), result);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.xor (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Operand &b = e.GetOperand(instr->arg1(), c.rax);
    c.xor (result, b);
  }
}

EMITTER(NOT) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  e.CopyOperand(instr->arg0(), result);

  c.not(result);
}

EMITTER(SHL) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  e.CopyOperand(instr->arg0(), result);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.shl(result, (int)instr->arg1()->GetZExtValue());
  } else {
    e.CopyOperand(instr->arg1(), c.cl);
    c.shl(result, c.cl);
  }
}

EMITTER(ASHR) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  e.CopyOperand(instr->arg0(), result);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.sar(result, (int)instr->arg1()->GetZExtValue());
  } else {
    e.CopyOperand(instr->arg1(), c.cl);
    c.sar(result, c.cl);
  }
}

EMITTER(LSHR) {
  const Xbyak::Operand &result = e.GetOperand(instr->result());

  e.CopyOperand(instr->arg0(), result);

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    c.shr(result, (int)instr->arg1()->GetZExtValue());
  } else {
    e.CopyOperand(instr->arg1(), c.cl);
    c.shr(result, c.cl);
  }
}

EMITTER(BRANCH) {
  if (instr->arg0()->type() == VALUE_BLOCK) {
    // jump to local block
    // TODO T_NEAR necessary?
    Block *dst = instr->arg0()->value<Block *>();
    int ordinal = dst->tag();
    c.jmp("." + std::to_string(ordinal), Xbyak::CodeGenerator::T_NEAR);
  } else {
    // return if we need to branch to a far block
    e.CopyOperand(instr->arg0(), c.rax);

    c.jmp(".epilog");
  }
}

EMITTER(BRANCH_COND) {
  const Xbyak::Reg &cond = e.GetRegister(instr->arg0(), c.rax);

  c.test(cond, cond);

  // TODO in most cases, arg0 or arg1 are going to be "next block", fall through
  // instead of having an explicit jump

  // if both blocks are a local block this is easy
  if (instr->arg1()->type() == VALUE_BLOCK &&
      instr->arg2()->type() == VALUE_BLOCK) {
    // jump to local block
    Block *block_true = instr->arg1()->value<Block *>();
    Block *block_false = instr->arg2()->value<Block *>();

    // TODO T_NEAR?
    c.jnz("." + std::to_string((int)block_true->tag()),
          Xbyak::CodeGenerator::T_NEAR);
    c.je("." + std::to_string((int)block_false->tag()),
         Xbyak::CodeGenerator::T_NEAR);
  }
  // if both blocks are a far block this is easy
  else if (instr->arg1()->type() != VALUE_BLOCK &&
           instr->arg2()->type() != VALUE_BLOCK) {
    // return if we need to branch to a far block
    const Xbyak::Operand &op_true = e.GetOperand(instr->arg1(), c.rax);
    const Xbyak::Operand &op_false = e.GetOperand(instr->arg2(), c.rcx);

    c.cmovnz(c.eax, op_true);
    c.cmovz(c.eax, op_false);
    c.jmp(".epilog");
  }
  // if they are mixed, do local block test first, far block second
  else {
    LOG(FATAL) << "Unsupported mixed mode conditional branch";
  }
}

EMITTER(CALL_EXTERNAL) {
  // rdi is already pointing to guest_ctx
  uint64_t addr = instr->arg0()->GetZExtValue();
  c.mov(c.rax, addr);
  c.call(c.rax);

  // restore rdi / rsi
  c.mov(Xbyak::util::rdi,
        c.qword[Xbyak::util::rsp + STACK_OFFSET_GUEST_CONTEXT]);
  c.mov(Xbyak::util::rsi, c.qword[Xbyak::util::rsp + STACK_OFFSET_MEMORY]);
}
