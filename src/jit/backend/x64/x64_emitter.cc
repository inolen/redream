#include <math.h>
#include "core/math.h"
#include "core/memory.h"
#include "emu/profiler.h"
#include "hw/memory.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/backend/x64/x64_emitter.h"

using namespace re;
using namespace re::hw;
using namespace re::jit;
using namespace re::jit::backend;
using namespace re::jit::backend::x64;
using namespace re::jit::ir;

const Xbyak::Reg64 arg0(x64_arg0_idx);
const Xbyak::Reg64 arg1(x64_arg1_idx);
const Xbyak::Reg64 arg2(x64_arg2_idx);
const Xbyak::Reg64 tmp0(x64_tmp0_idx);
const Xbyak::Reg64 tmp1(x64_tmp1_idx);

// callbacks for emitting each IR op
typedef void (*X64Emit)(X64Emitter &, const Instr *);

static X64Emit x64_emitters[NUM_OPS];

#define EMITTER(op)                                     \
  void op(X64Emitter &, const Instr *);                 \
  static struct _x64_##op##_init {                      \
    _x64_##op##_init() { x64_emitters[OP_##op] = &op; } \
  } x64_##op##_init;                                    \
  void op(X64Emitter &e, const Instr *instr)

static bool IsCalleeSaved(const Xbyak::Reg &reg) {
  if (reg.isXMM()) {
    return false;
  }

  static bool callee_saved[16] = {
    false,  // RAX
    false,  // RCX
    false,  // RDX
    true,   // RBX
#if PLATFORM_WINDOWS
    true,  // RSP
#else
    false,  // RSP
#endif
    true,  // RBP
#if PLATFORM_WINDOWS
    true,  // RSI
#else
    false,  // RSI
#endif
    false,  // RDI
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

X64Emitter::X64Emitter(void *buffer, size_t buffer_size)
    : CodeGenerator(buffer_size, buffer),
      arena_(1024),
      memory_(nullptr),
      guest_ctx_(nullptr),
      block_flags_(0) {
  // temporary registers aren't tracked to be pushed and popped
  CHECK(!IsCalleeSaved(tmp0) && !IsCalleeSaved(tmp1));

  modified_ = new int[x64_num_registers];

  Reset();
}

X64Emitter::~X64Emitter() { delete[] modified_; }

void X64Emitter::Reset() {
  reset();

  modified_marker_ = 0;
  memset(modified_, modified_marker_, sizeof(int) * x64_num_registers);
}

BlockPointer X64Emitter::Emit(IRBuilder &builder, Memory &memory,
                              void *guest_ctx, int block_flags) {
  PROFILER_RUNTIME("X64Emitter::Emit");

  // save off parameters for ease of access
  memory_ = &memory;
  guest_ctx_ = guest_ctx;
  block_flags_ = block_flags;

  // getCurr returns the current spot in the codegen buffer which the function
  // is about to emitted to
  BlockPointer fn = getCurr<BlockPointer>();

  // reset emit state
  arena_.Reset();

  int stack_size = 0;
  EmitProlog(builder, &stack_size);
  EmitBody(builder);
  EmitEpilog(builder, stack_size);
  ready();

  return fn;
}

void X64Emitter::EmitProlog(IRBuilder &builder, int *out_stack_size) {
  int stack_size = STACK_SIZE;

  // align locals
  for (auto local : builder.locals()) {
    int type_size = SizeForType(local->type());
    stack_size = re::align_up(stack_size, type_size);
    local->set_offset(builder.AllocConstant(stack_size));
    stack_size += type_size;
  }

  // stack must be 16 byte aligned
  stack_size = re::align_up(stack_size, 16);

  // add 8 for return address which will be pushed when this is called
  stack_size += 8;

  CHECK_EQ((stack_size + 8) % 16, 0);

  // mark which registers have been modified
  modified_marker_++;

  for (auto instr : builder.instrs()) {
    int i = instr->reg();
    if (i == NO_REGISTER) {
      continue;
    }

    modified_[i] = modified_marker_;
  }

  // push the callee-saved registers which have been modified
  int pushed = 0;

  for (int i = 0; i < x64_num_registers; i++) {
    const Xbyak::Reg &reg =
        *reinterpret_cast<const Xbyak::Reg *>(x64_registers[i].data);

    if (IsCalleeSaved(reg) && modified_[i] == modified_marker_) {
      push(reg);
      pushed++;
    }
  }

  // if an odd amount of push instructions are emitted stack_size needs to be
  // adjusted to keep the stack aligned
  if ((pushed % 2) == 1) {
    stack_size += 8;
  }

  // adjust stack pointer
  sub(rsp, stack_size);

  // copy guest context and memory base to argument registers
  mov(r10, reinterpret_cast<uint64_t>(guest_ctx_));
  mov(r11, reinterpret_cast<uint64_t>(memory_->protected_base()));

  *out_stack_size = stack_size;
}

void X64Emitter::EmitBody(IRBuilder &builder) {
  for (auto instr : builder.instrs()) {
    X64Emit emit = x64_emitters[instr->op()];
    CHECK(emit, "Failed to find emitter for %s", Opnames[instr->op()]);

    // reset temp count used by GetRegister
    num_temps_ = 0;

    emit(*this, instr);
  }
}

void X64Emitter::EmitEpilog(IRBuilder &builder, int stack_size) {
  // adjust stack pointer
  add(rsp, stack_size);

  // pop callee-saved registers which have been modified
  for (int i = x64_num_registers - 1; i >= 0; i--) {
    const Xbyak::Reg &reg =
        *reinterpret_cast<const Xbyak::Reg *>(x64_registers[i].data);

    if (IsCalleeSaved(reg) && modified_[i] == modified_marker_) {
      pop(reg);
    }
  }

  ret();
}

// If the value is a local or constant, copy it to a tempory register, else
// return the register allocated for it.
const Xbyak::Reg X64Emitter::GetRegister(const Value *v) {
  if (v->constant()) {
    CHECK_LT(num_temps_, 2);

    Xbyak::Reg tmp = num_temps_++ ? tmp1 : tmp0;

    switch (v->type()) {
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
    CopyOperand(v, tmp);

    return tmp;
  }

  int i = v->reg();
  CHECK_NE(i, NO_REGISTER);

  const Xbyak::Reg &reg =
      *reinterpret_cast<const Xbyak::Reg *>(x64_registers[i].data);
  CHECK(reg.isREG());

  switch (v->type()) {
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

// If the value isn't allocated a XMM register copy it to a temporary XMM,
// register, else return the XMM register allocated for it.
const Xbyak::Xmm X64Emitter::GetXMMRegister(const Value *v) {
  if (v->constant()) {
    CopyOperand(v, xmm1);
    return xmm1;
  }

  int i = v->reg();
  CHECK_NE(i, NO_REGISTER);

  const Xbyak::Xmm &xmm =
      *reinterpret_cast<const Xbyak::Xmm *>(x64_registers[i].data);
  CHECK(xmm.isXMM());
  return xmm;
}

// Copy the value to the supplied operand.
void X64Emitter::CopyOperand(const Value *v, const Xbyak::Reg &to) {
  if (v->constant()) {
    if (to.isXMM()) {
      CHECK(IsFloatType(v->type()));

      if (v->type() == VALUE_F32) {
        float val = v->f32();
        mov(eax, re::load<int32_t>(&val));
        movd(reinterpret_cast<const Xbyak::Xmm &>(to), eax);
      } else {
        double val = v->f64();
        mov(rax, re::load<int64_t>(&val));
        movq(reinterpret_cast<const Xbyak::Xmm &>(to), rax);
      }
    } else {
      CHECK(IsIntType(v->type()));

      mov(to, v->GetZExtValue());
    }
  } else if (IsFloatType(v->type())) {
    const Xbyak::Xmm from = GetXMMRegister(v);

    if (from != to) {
      if (to.isXMM()) {
        movdqa(reinterpret_cast<const Xbyak::Xmm &>(to), from);
      } else if (to.isMEM() && to.isBit(32)) {
        movss(reinterpret_cast<const Xbyak::Address &>(to), from);
      } else if (to.isMEM() && to.isBit(64)) {
        movsd(reinterpret_cast<const Xbyak::Address &>(to), from);
      } else {
        LOG_FATAL("Unexpected copy");
      }
    }
  } else {
    const Xbyak::Reg from = GetRegister(v);

    if (from != to) {
      if (to.isXMM() && from.isBit(32)) {
        movss(reinterpret_cast<const Xbyak::Xmm &>(to), from);
      } else if (to.isXMM() && from.isBit(64)) {
        movsd(reinterpret_cast<const Xbyak::Xmm &>(to), from);
      } else {
        mov(to, from);
      }
    }
  }
}

Xbyak::Label *X64Emitter::AllocLabel() {
  Xbyak::Label *label = arena_.Alloc<Xbyak::Label>();
  new (label) Xbyak::Label();
  return label;
}

bool X64Emitter::CanEncodeAsImmediate(const Value *v) const {
  if (!v->constant()) {
    return false;
  }

  return v->type() <= VALUE_I32;
}

EMITTER(LOAD_HOST) {
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (IsFloatType(instr->type())) {
    const Xbyak::Xmm result = e.GetXMMRegister(instr);

    switch (instr->type()) {
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
    const Xbyak::Reg result = e.GetRegister(instr);

    switch (instr->type()) {
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
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (IsFloatType(instr->arg1()->type())) {
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    switch (instr->arg1()->type()) {
      case VALUE_F32:
        e.movss(e.dword[a], b);
        break;
      case VALUE_F64:
        e.movsd(e.qword[a], b);
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());

    switch (instr->arg1()->type()) {
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

EMITTER(LOAD_GUEST) {
  const Xbyak::Reg result = e.GetRegister(instr);

  if (instr->arg0()->constant()) {
    // try to resolve the address to a physical page
    uint32_t addr = static_cast<uint32_t>(instr->arg0()->i32());
    uint8_t *host_addr = nullptr;
    MemoryRegion *region = nullptr;
    uint32_t offset = 0;

    e.memory()->Lookup(addr, &host_addr, &region, &offset);

    // if the address maps to a physical page, not a dynamic handler, make it
    // fast
    if (host_addr) {
      // FIXME it'd be nice if xbyak had a mov operation which would convert
      // the displacement to a RIP-relative address when finalizing code so
      // we didn't have to store the absolute address in the scratch register
      e.mov(e.rax, reinterpret_cast<uint64_t>(host_addr));

      switch (instr->type()) {
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

  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (e.block_flags() & BF_SLOWMEM) {
    void *fn = nullptr;
    switch (instr->type()) {
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

    e.mov(arg0, reinterpret_cast<uint64_t>(e.memory()));
    e.mov(arg1, a);
    e.call(reinterpret_cast<void *>(fn));
    e.mov(result, e.rax);

    // restore context register
    e.mov(e.r10, reinterpret_cast<uint64_t>(e.guest_ctx()));
    e.mov(e.r11, reinterpret_cast<uint64_t>(e.memory()->protected_base()));
  } else {
    switch (instr->type()) {
      case VALUE_I8:
        e.mov(result, e.byte[a.cvt64() + e.r11]);
        break;
      case VALUE_I16:
        e.mov(result, e.word[a.cvt64() + e.r11]);
        break;
      case VALUE_I32:
        e.mov(result, e.dword[a.cvt64() + e.r11]);
        break;
      case VALUE_I64:
        e.mov(result, e.qword[a.cvt64() + e.r11]);
        break;
      default:
        LOG_FATAL("Unexpected load result type");
        break;
    }
  }
}

EMITTER(STORE_GUEST) {
  if (instr->arg0()->constant()) {
    // try to resolve the address to a physical page
    uint32_t addr = static_cast<uint32_t>(instr->arg0()->i32());
    uint8_t *host_addr = nullptr;
    MemoryRegion *bank = nullptr;
    uint32_t offset = 0;

    e.memory()->Lookup(addr, &host_addr, &bank, &offset);

    if (host_addr) {
      const Xbyak::Reg b = e.GetRegister(instr->arg1());

      // FIXME it'd be nice if xbyak had a mov operation which would convert
      // the displacement to a RIP-relative address when finalizing code so
      // we didn't have to store the absolute address in the scratch register
      e.mov(e.rax, reinterpret_cast<uint64_t>(host_addr));

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

  const Xbyak::Reg a = e.GetRegister(instr->arg0());
  const Xbyak::Reg b = e.GetRegister(instr->arg1());

  if (e.block_flags() & BF_SLOWMEM) {
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

    e.mov(arg0, reinterpret_cast<uint64_t>(e.memory()));
    e.mov(arg1, a);
    e.mov(arg2, b);
    e.call(reinterpret_cast<void *>(fn));

    // restore context register
    e.mov(e.r10, reinterpret_cast<uint64_t>(e.guest_ctx()));
    e.mov(e.r11, reinterpret_cast<uint64_t>(e.memory()->protected_base()));
  } else {
    switch (instr->arg1()->type()) {
      case VALUE_I8:
        e.mov(e.byte[a.cvt64() + e.r11], b);
        break;
      case VALUE_I16:
        e.mov(e.word[a.cvt64() + e.r11], b);
        break;
      case VALUE_I32:
        e.mov(e.dword[a.cvt64() + e.r11], b);
        break;
      case VALUE_I64:
        e.mov(e.qword[a.cvt64() + e.r11], b);
        break;
      default:
        LOG_FATAL("Unexpected store value type");
        break;
    }
  }
}

EMITTER(LOAD_CONTEXT) {
  int offset = instr->arg0()->i32();

  if (IsFloatType(instr->type())) {
    const Xbyak::Xmm result = e.GetXMMRegister(instr);

    switch (instr->type()) {
      case VALUE_F32:
        e.movss(result, e.dword[e.r10 + offset]);
        break;
      case VALUE_F64:
        e.movsd(result, e.qword[e.r10 + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  } else {
    const Xbyak::Reg result = e.GetRegister(instr);

    switch (instr->type()) {
      case VALUE_I8:
        e.mov(result, e.byte[e.r10 + offset]);
        break;
      case VALUE_I16:
        e.mov(result, e.word[e.r10 + offset]);
        break;
      case VALUE_I32:
        e.mov(result, e.dword[e.r10 + offset]);
        break;
      case VALUE_I64:
        e.mov(result, e.qword[e.r10 + offset]);
        break;
      default:
        LOG_FATAL("Unexpected result type");
        break;
    }
  }
}

EMITTER(STORE_CONTEXT) {
  int offset = instr->arg0()->i32();

  if (instr->arg1()->constant()) {
    switch (instr->arg1()->type()) {
      case VALUE_I8:
        e.mov(e.byte[e.r10 + offset], instr->arg1()->i8());
        break;
      case VALUE_I16:
        e.mov(e.word[e.r10 + offset], instr->arg1()->i16());
        break;
      case VALUE_I32:
      case VALUE_F32:
        e.mov(e.dword[e.r10 + offset], instr->arg1()->i32());
        break;
      case VALUE_I64:
      case VALUE_F64:
        e.mov(e.qword[e.r10 + offset], instr->arg1()->i64());
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else {
    if (IsFloatType(instr->arg1()->type())) {
      const Xbyak::Xmm src = e.GetXMMRegister(instr->arg1());

      switch (instr->arg1()->type()) {
        case VALUE_F32:
          e.movss(e.dword[e.r10 + offset], src);
          break;
        case VALUE_F64:
          e.movsd(e.qword[e.r10 + offset], src);
          break;
        default:
          LOG_FATAL("Unexpected value type");
          break;
      }
    } else {
      const Xbyak::Reg src = e.GetRegister(instr->arg1());

      switch (instr->arg1()->type()) {
        case VALUE_I8:
          e.mov(e.byte[e.r10 + offset], src);
          break;
        case VALUE_I16:
          e.mov(e.word[e.r10 + offset], src);
          break;
        case VALUE_I32:
          e.mov(e.dword[e.r10 + offset], src);
          break;
        case VALUE_I64:
          e.mov(e.qword[e.r10 + offset], src);
          break;
        default:
          LOG_FATAL("Unexpected value type");
          break;
      }
    }
  }
}

EMITTER(LOAD_LOCAL) {
  int offset = instr->arg0()->i32();

  if (IsFloatType(instr->type())) {
    const Xbyak::Xmm result = e.GetXMMRegister(instr);

    switch (instr->type()) {
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
    const Xbyak::Reg result = e.GetRegister(instr);

    switch (instr->type()) {
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
  int offset = instr->arg0()->i32();

  CHECK(!instr->arg1()->constant());

  if (IsFloatType(instr->arg1()->type())) {
    const Xbyak::Xmm src = e.GetXMMRegister(instr->arg1());

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
    const Xbyak::Reg src = e.GetRegister(instr->arg1());

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

EMITTER(BITCAST) {
  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (result.getIdx() == a.getIdx()) {
    // noop if already the same register
    return;
  }

  e.mov(result.cvt64(), a.cvt64());
}

EMITTER(CAST) {
  if (IsFloatType(instr->type())) {
    const Xbyak::Xmm result = e.GetXMMRegister(instr);
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    switch (instr->type()) {
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
    const Xbyak::Reg result = e.GetRegister(instr);
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());

    switch (instr->type()) {
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
  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

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
  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

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

EMITTER(SELECT) {
  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg cond = e.GetRegister(instr->arg0());
  const Xbyak::Reg a = e.GetRegister(instr->arg1());
  const Xbyak::Reg b = e.GetRegister(instr->arg2());

  e.test(cond, cond);
  e.cmovnz(result.cvt32(), a);
  e.cmovz(result.cvt32(), b);
}

EMITTER(EQ) {
  const Xbyak::Reg result = e.GetRegister(instr);

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }
  } else {
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }
  }

  e.sete(result);
}

EMITTER(NE) {
  const Xbyak::Reg result = e.GetRegister(instr);

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }
  } else {
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }
  }

  e.setne(result);
}

EMITTER(SGE) {
  const Xbyak::Reg result = e.GetRegister(instr);

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }

    e.setae(result);
  } else {
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }

    e.setge(result);
  }
}

EMITTER(SGT) {
  const Xbyak::Reg result = e.GetRegister(instr);

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }

    e.seta(result);
  } else {
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }

    e.setg(result);
  }
}

EMITTER(UGE) {
  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.cmp(a, b);
  }

  e.setae(result);
}

EMITTER(UGT) {
  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.cmp(a, b);
  }

  e.seta(result);
}

EMITTER(SLE) {
  const Xbyak::Reg result = e.GetRegister(instr);

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }

    e.setbe(result);
  } else {
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }

    e.setle(result);
  }
}

EMITTER(SLT) {
  const Xbyak::Reg result = e.GetRegister(instr);

  if (IsFloatType(instr->arg0()->type())) {
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    if (instr->arg0()->type() == VALUE_F32) {
      e.comiss(a, b);
    } else {
      e.comisd(a, b);
    }

    e.setb(result);
  } else {
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg b = e.GetRegister(instr->arg1());
      e.cmp(a, b);
    }

    e.setl(result);
  }
}

EMITTER(ULE) {
  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.cmp(a, b);
  }

  e.setbe(result);
}

EMITTER(ULT) {
  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.cmp(a, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.cmp(a, b);
  }

  e.setb(result);
}

EMITTER(ADD) {
  if (IsFloatType(instr->type())) {
    const Xbyak::Xmm result = e.GetXMMRegister(instr);
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    if (instr->type() == VALUE_F32) {
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
    const Xbyak::Reg result = e.GetRegister(instr);
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    if (result != a) {
      e.mov(result, a);
    }

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.add(result, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg b = e.GetRegister(instr->arg1());
      e.add(result, b);
    }
  }
}

EMITTER(SUB) {
  if (IsFloatType(instr->type())) {
    const Xbyak::Xmm result = e.GetXMMRegister(instr);
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    if (instr->type() == VALUE_F32) {
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
    const Xbyak::Reg result = e.GetRegister(instr);
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    if (result != a) {
      e.mov(result, a);
    }

    if (e.CanEncodeAsImmediate(instr->arg1())) {
      e.sub(result, (uint32_t)instr->arg1()->GetZExtValue());
    } else {
      const Xbyak::Reg b = e.GetRegister(instr->arg1());
      e.sub(result, b);
    }
  }
}

EMITTER(SMUL) {
  if (IsFloatType(instr->type())) {
    const Xbyak::Xmm result = e.GetXMMRegister(instr);
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
    const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

    if (instr->type() == VALUE_F32) {
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
    const Xbyak::Reg result = e.GetRegister(instr);
    const Xbyak::Reg a = e.GetRegister(instr->arg0());
    const Xbyak::Reg b = e.GetRegister(instr->arg1());

    if (result != a) {
      e.mov(result, a);
    }

    e.imul(result, b);
  }
}

EMITTER(UMUL) {
  CHECK(IsIntType(instr->type()));

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());
  const Xbyak::Reg b = e.GetRegister(instr->arg1());

  if (result != a) {
    e.mov(result, a);
  }

  e.imul(result, b);
}

EMITTER(DIV) {
  CHECK(IsFloatType(instr->type()));

  const Xbyak::Xmm result = e.GetXMMRegister(instr);
  const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());
  const Xbyak::Xmm b = e.GetXMMRegister(instr->arg1());

  if (instr->type() == VALUE_F32) {
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
  if (IsFloatType(instr->type())) {
    const Xbyak::Xmm result = e.GetXMMRegister(instr);
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());

    if (instr->type() == VALUE_F32) {
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
    const Xbyak::Reg result = e.GetRegister(instr);
    const Xbyak::Reg a = e.GetRegister(instr->arg0());

    if (result != a) {
      e.mov(result, a);
    }

    e.neg(result);
  }
}

EMITTER(SQRT) {
  CHECK(IsFloatType(instr->type()));

  const Xbyak::Xmm result = e.GetXMMRegister(instr);
  const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());

  if (instr->type() == VALUE_F32) {
    e.sqrtss(result, a);
  } else {
    e.sqrtsd(result, a);
  }
}

EMITTER(ABS) {
  if (IsFloatType(instr->type())) {
    const Xbyak::Xmm result = e.GetXMMRegister(instr);
    const Xbyak::Xmm a = e.GetXMMRegister(instr->arg0());

    if (instr->type() == VALUE_F32) {
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

EMITTER(AND) {
  CHECK(IsIntType(instr->type()));

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.and (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.and (result, b);
  }
}

EMITTER(OR) {
  CHECK(IsIntType(instr->type()));

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.or (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.or (result, b);
  }
}

EMITTER(XOR) {
  CHECK(IsIntType(instr->type()));

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.xor (result, (uint32_t)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.xor (result, b);
  }
}

EMITTER(NOT) {
  CHECK(IsIntType(instr->type()));

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  e.not(result);
}

EMITTER(SHL) {
  CHECK(IsIntType(instr->type()));

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.shl(result, (int)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.mov(e.cl, b);
    e.shl(result, e.cl);
  }
}

EMITTER(ASHR) {
  CHECK(IsIntType(instr->type()));

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.sar(result, (int)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.mov(e.cl, b);
    e.sar(result, e.cl);
  }
}

EMITTER(LSHR) {
  CHECK(IsIntType(instr->type()));

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg a = e.GetRegister(instr->arg0());

  if (result != a) {
    e.mov(result, a);
  }

  if (e.CanEncodeAsImmediate(instr->arg1())) {
    e.shr(result, (int)instr->arg1()->GetZExtValue());
  } else {
    const Xbyak::Reg b = e.GetRegister(instr->arg1());
    e.mov(e.cl, b);
    e.shr(result, e.cl);
  }
}

EMITTER(ASHD) {
  CHECK_EQ(instr->type(), VALUE_I32);

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg v = e.GetRegister(instr->arg0());
  const Xbyak::Reg n = e.GetRegister(instr->arg1());

  Xbyak::Label *shr_label = e.AllocLabel();
  Xbyak::Label *shr_overflow_label = e.AllocLabel();
  Xbyak::Label *end_label = e.AllocLabel();

  if (result != v) {
    e.mov(result, v);
  }

  // check if we're shifting left or right
  e.test(n, 0x80000000);
  e.jnz(*shr_label);

  // perform shift left
  e.mov(e.cl, n);
  e.sal(result, e.cl);
  e.jmp(*end_label);

  // perform right shift
  e.L(*shr_label);
  e.test(n, 0x1f);
  e.jz(*shr_overflow_label);
  e.mov(e.cl, n);
  e.neg(e.cl);
  e.sar(result, e.cl);
  e.jmp(*end_label);

  // right shift overflowed
  e.L(*shr_overflow_label);
  e.sar(result, 31);

  // shift is done
  e.L(*end_label);
}

EMITTER(LSHD) {
  CHECK_EQ(instr->type(), VALUE_I32);

  const Xbyak::Reg result = e.GetRegister(instr);
  const Xbyak::Reg v = e.GetRegister(instr->arg0());
  const Xbyak::Reg n = e.GetRegister(instr->arg1());

  Xbyak::Label *shr_label = e.AllocLabel();
  Xbyak::Label *shr_overflow_label = e.AllocLabel();
  Xbyak::Label *end_label = e.AllocLabel();

  if (result != v) {
    e.mov(result, v);
  }

  // check if we're shifting left or right
  e.test(n, 0x80000000);
  e.jnz(*shr_label);

  // perform shift left
  e.mov(e.cl, n);
  e.shl(result, e.cl);
  e.jmp(*end_label);

  // perform right shift
  e.L(*shr_label);
  e.test(n, 0x1f);
  e.jz(*shr_overflow_label);
  e.mov(e.cl, n);
  e.neg(e.cl);
  e.shr(result, e.cl);
  e.jmp(*end_label);

  // right shift overflowed
  e.L(*shr_overflow_label);
  e.mov(result, 0x0);

  // shift is done
  e.L(*end_label);
}

EMITTER(CALL_EXTERNAL) {
  e.mov(arg0, reinterpret_cast<uint64_t>(e.guest_ctx()));

  // if an additional argument is specified, copy it into the register for arg1
  if (instr->arg1()) {
    const Xbyak::Reg arg = e.GetRegister(instr->arg1());
    e.mov(arg1, arg);
  }

  // call the external function
  e.CopyOperand(instr->arg0(), e.rax);
  e.call(e.rax);

  // restore context register
  e.mov(e.r10, reinterpret_cast<uint64_t>(e.guest_ctx()));
  e.mov(e.r11, reinterpret_cast<uint64_t>(e.memory()->protected_base()));
}
