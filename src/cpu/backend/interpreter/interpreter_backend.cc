#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/backend/interpreter/interpreter_callbacks.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::backend;
using namespace dreavm::cpu::backend::interpreter;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

// fake registers for testing register allocation
static Register int_registers[NUM_INT_REGS] = {{"a", VALUE_ALL_MASK},
                                               {"b", VALUE_ALL_MASK},
                                               {"c", VALUE_ALL_MASK},
                                               {"d", VALUE_ALL_MASK}};

static IntSig GetSignature(Instr &ir_i) {
  IntSig sig = 0;

  auto set_sig = [&](int arg) {
    Value *ir_v = ir_i.arg(arg);
    if (!ir_v) {
      return;
    }

    ValueTy type = ir_v->type();

    // blocks are translated to int32 offsets
    if (type == VALUE_BLOCK) {
      type = VALUE_I32;
    }

    SetArgSignature(arg, type, &sig);
  };

  set_sig(0);
  set_sig(1);
  set_sig(2);
  set_sig(3);

  return sig;
}

static IntAccessMask GetAccessMask(Instr &ir_i) {
  IntAccessMask access_mask = 0;

  auto set_access = [&](int arg) {
    Value *ir_v = ir_i.arg(arg);
    if (!ir_v) {
      return;
    }
    if (ir_v->constant()) {
      SetArgAccess(arg, ACC_IMM, &access_mask);
    } else if (ir_v->reg() != NO_REGISTER) {
      SetArgAccess(arg, ACC_REG, &access_mask);
    } else if (ir_v->local() != NO_SLOT) {
      SetArgAccess(arg, ACC_LCL, &access_mask);
    } else {
      CHECK(false && "Unexpected value type");
    }
  };

  set_access(0);
  set_access(1);
  set_access(2);
  set_access(3);

  return access_mask;
}

AssembleContext::AssembleContext()
    : max_instrs(4),
      num_instrs(0),
      instrs(
          reinterpret_cast<IntInstr *>(malloc(max_instrs * sizeof(IntInstr)))) {
}

AssembleContext::~AssembleContext() { free(instrs); }

IntInstr *AssembleContext::AllocInstr() {
  if (num_instrs >= max_instrs) {
    max_instrs *= 2;
    instrs = reinterpret_cast<IntInstr *>(
        realloc(instrs, max_instrs * sizeof(IntInstr)));
  }
  IntInstr *i = &instrs[num_instrs++];
  memset(i, 0, sizeof(*i));
  return i;
}

IntInstr *AssembleContext::TranslateInstr(Instr &ir_i) {
  IntInstr *i = AllocInstr();

  TranslateArg(ir_i, i, 0);
  TranslateArg(ir_i, i, 1);
  TranslateArg(ir_i, i, 2);
  TranslateArg(ir_i, i, 3);

  i->fn = GetCallback(ir_i.op(), GetSignature(ir_i), GetAccessMask(ir_i));

  i->guest_addr = ir_i.guest_addr;
  i->guest_op = ir_i.guest_op;

  return i;
}

void AssembleContext::TranslateArg(Instr &ir_i, IntInstr *i, int arg) {
  Value *ir_v = ir_i.arg(arg);

  if (!ir_v) {
    return;
  }

  IntValue *v = &i->arg[arg];

  if (ir_v->constant()) {
    switch (ir_v->type()) {
      case VALUE_I8:
        v->i8 = ir_v->value<int8_t>();
        break;
      case VALUE_I16:
        v->i16 = ir_v->value<int16_t>();
        break;
      case VALUE_I32:
        v->i32 = ir_v->value<int32_t>();
        break;
      case VALUE_I64:
        v->i64 = ir_v->value<int64_t>();
        break;
      case VALUE_F32:
        v->f32 = ir_v->value<float>();
        break;
      case VALUE_F64:
        v->f64 = ir_v->value<double>();
        break;
      case VALUE_BLOCK:
        v->i32 = (int32_t)ir_v->value<Block *>()->instrs().head()->tag();
        break;
    }
  } else if (ir_v->reg() != NO_REGISTER) {
    v->i32 = ir_v->reg();
  } else if (ir_v->local() != NO_SLOT) {
    v->i32 = ir_v->local();
  } else {
    CHECK(false && "Unexpected value type");
  }
}

InterpreterBackend::InterpreterBackend(emu::Memory &memory) : Backend(memory) {}

InterpreterBackend::~InterpreterBackend() {}

const Register *InterpreterBackend::registers() const { return int_registers; }

int InterpreterBackend::num_registers() const {
  return sizeof(int_registers) / sizeof(Register);
}

bool InterpreterBackend::Init() { return true; }

std::unique_ptr<RuntimeBlock> InterpreterBackend::AssembleBlock(
    IRBuilder &builder) {
  AssembleContext ctx;

  // do an initial pass assigning ordinals to instructions so local branches
  // can be resolved
  int32_t ordinal = 0;
  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      instr->set_tag((intptr_t)ordinal++);
    }
  }

  // translate each instruction
  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      ctx.TranslateInstr(*instr);
    }
  }

  // get number of guest cycles for the blocks
  const Value *md_guest_cycles = builder.GetMetadata(MD_GUEST_CYCLES);
  CHECK(md_guest_cycles);
  int guest_cycles = md_guest_cycles->value<int32_t>();

  // take ownership of ctx pointers
  IntInstr *instrs = ctx.instrs;
  int num_instrs = ctx.num_instrs;
  ctx.instrs = nullptr;

  return std::unique_ptr<RuntimeBlock>(new InterpreterBlock(
      guest_cycles, instrs, num_instrs, builder.locals_size()));
}
