#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/backend/interpreter/interpreter_callbacks.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::interpreter;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

AssembleContext::AssembleContext()
    : max_instrs(4),
      num_instrs(0),
      instrs(
          reinterpret_cast<IntInstr *>(malloc(max_instrs * sizeof(IntInstr)))),
      num_registers(1) {}

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

int AssembleContext::AllocRegister() { return num_registers++; }

IntInstr *AssembleContext::TranslateInstr(Instr &ir_i) {
  IntInstr *i = AllocInstr();

  uint32_t imm_mask = 0;
  TranslateArg(ir_i, i, 0, &imm_mask);
  TranslateArg(ir_i, i, 1, &imm_mask);
  TranslateArg(ir_i, i, 2, &imm_mask);
  if (ir_i.result()) {
    int r = AllocRegister();
    ir_i.result()->set_tag((intptr_t)r);
    i->result = r;
  }

  i->fn = GetCallback(ir_i.op(), GetSignature(ir_i), imm_mask);

  i->guest_addr = ir_i.guest_addr;
  i->guest_op = ir_i.guest_op;

  return i;
}

IntSig AssembleContext::GetSignature(Instr &ir_i) {
  static const int types[] = {
      SIG_I8,   // VALUE_I8
      SIG_I16,  // VALUE_I16
      SIG_I32,  // VALUE_I32
      SIG_I64,  // VALUE_I64
      SIG_F32,  // VALUE_F32
      SIG_F64,  // VALUE_F64
      SIG_I32,  // VALUE_BLOCK
  };

  IntSig sig;

  sig.full = 0;

  if (ir_i.result()) {
    sig.result = types[ir_i.result()->type()];
  }
  if (ir_i.arg0()) {
    sig.arg0 = types[ir_i.arg0()->type()];
  }
  if (ir_i.arg1()) {
    sig.arg1 = types[ir_i.arg1()->type()];
  }
  if (ir_i.arg2()) {
    sig.arg2 = types[ir_i.arg2()->type()];
  }

  return sig;
}

void AssembleContext::TranslateArg(Instr &ir_i, IntInstr *i, int arg,
                                   uint32_t *imm_mask) {
  Value *ir_v = ir_i.arg(arg);

  if (!ir_v) {
    return;
  }

  IntReg *r = &i->arg[arg];

  if (!ir_v->constant()) {
    // for non-constant values, generate a shared register that will be stored
    // at runtime
    if (!ir_v->tag()) {
      ir_v->set_tag((intptr_t)AllocRegister());
      // CHECK(ir_v->tag);
    }
    r->i32 = (int)(intptr_t)ir_v->tag();
    return;
  }

  *imm_mask |= (1 << arg);

  switch (ir_v->type()) {
    case VALUE_I8:
      r->i8 = ir_v->value<int8_t>();
      break;
    case VALUE_I16:
      r->i16 = ir_v->value<int16_t>();
      break;
    case VALUE_I32:
      r->i32 = ir_v->value<int32_t>();
      break;
    case VALUE_I64:
      r->i64 = ir_v->value<int64_t>();
      break;
    case VALUE_F32:
      r->f32 = ir_v->value<float>();
      break;
    case VALUE_F64:
      r->f64 = ir_v->value<double>();
      break;
    case VALUE_BLOCK:
      r->i32 = (int32_t)ir_v->value<Block *>()->instrs().head()->tag();
      break;
  }
}

InterpreterBackend::InterpreterBackend(emu::Memory &memory) : Backend(memory) {}

InterpreterBackend::~InterpreterBackend() {}

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
      guest_cycles, instrs, num_instrs, ctx.num_registers));
}
