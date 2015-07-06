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
      max_block_refs(4),
      num_block_refs(0),
      block_refs(reinterpret_cast<BlockRef *>(
          malloc(max_block_refs * sizeof(BlockRef)))),
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

BlockRef *AssembleContext::AllocBlockRef() {
  if (num_block_refs >= max_block_refs) {
    max_block_refs *= 2;
    block_refs = reinterpret_cast<BlockRef *>(
        realloc(block_refs, max_block_refs * sizeof(BlockRef)));
  }
  BlockRef *ref = &block_refs[num_block_refs++];
  memset(ref, 0, sizeof(*ref));
  return ref;
}

int AssembleContext::AllocRegister() { return num_registers++; }

IntInstr *AssembleContext::TranslateInstr(Instr &ir_i, IntFn fn) {
  // tag source instruction with the offset to help in resolving block
  // references to instruction offsets
  ir_i.set_tag(num_instrs);

  IntInstr *i = AllocInstr();
  i->fn = fn;
  TranslateValue(ir_i.arg0(), &i->arg[0]);
  TranslateValue(ir_i.arg1(), &i->arg[1]);
  TranslateValue(ir_i.arg2(), &i->arg[2]);
  if (ir_i.result()) {
    int r = AllocRegister();
    ir_i.result()->set_tag((intptr_t)r);
    i->result = r;
  }

  i->guest_addr = ir_i.guest_addr;
  i->guest_op = ir_i.guest_op;

  return i;
}

void AssembleContext::TranslateValue(Value *ir_v, IntReg *r) {
  if (!ir_v) {
    r->i32 = 0;
    return;
  }

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
      // this argument references a block which is part of this IRBuilder.
      // generate a register which will be backpatched with an actual value
      // later on.
      BlockRef *ref = AllocBlockRef();
      ref->block = ir_v->value<Block *>();
      // store register address as offset since instrs is constantly realloced
      ref->offset =
          reinterpret_cast<uint8_t *>(r) - reinterpret_cast<uint8_t *>(instrs);
      r->i32 = 0;
      break;
  }

  return;
}

InterpreterBackend::InterpreterBackend(emu::Memory &memory) : Backend(memory) {}

InterpreterBackend::~InterpreterBackend() {}

bool InterpreterBackend::Init() { return true; }

std::unique_ptr<RuntimeBlock> InterpreterBackend::AssembleBlock(
    IRBuilder &builder) {
  AssembleContext ctx;

  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      ctx.TranslateInstr(*instr, GetCallback(instr));
    }
  }

  // backpatch register values containing local block addresses
  for (int i = 0; i < ctx.num_block_refs; i++) {
    BlockRef *ref = &ctx.block_refs[i];
    IntReg *r = reinterpret_cast<IntReg *>(
        reinterpret_cast<uint8_t *>(ctx.instrs) + ref->offset);
    r->i32 = (int32_t)ref->block->instrs().head()->tag();
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
