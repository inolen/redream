#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/interpreter/interpreter_block.h"
#include "jit/backend/interpreter/interpreter_callbacks.h"

using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend;
using namespace dreavm::jit::backend::interpreter;
using namespace dreavm::jit::ir;

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
    } else {
      LOG_FATAL("Unexpected value type");
    }
  };

  set_access(0);
  set_access(1);
  set_access(2);
  set_access(3);

  return access_mask;
}

InterpreterBackend::InterpreterBackend(Memory &memory) : Backend(memory) {
  static const int codegen_size = 1024 * 1024 * 8;
  codegen_begin_ = new uint8_t[codegen_size];
  codegen_end_ = codegen_begin_ + codegen_size;
  codegen_ = codegen_begin_;
}

InterpreterBackend::~InterpreterBackend() { delete[] codegen_begin_; }

const Register *InterpreterBackend::registers() const { return int_registers; }

int InterpreterBackend::num_registers() const {
  return sizeof(int_registers) / sizeof(Register);
}

void InterpreterBackend::Reset() { codegen_ = codegen_begin_; }

std::unique_ptr<RuntimeBlock> InterpreterBackend::AssembleBlock(
    ir::IRBuilder &builder) {
  // do an initial pass assigning ordinals to instructions so local branches
  // can be resolved
  int32_t ordinal = 0;
  for (auto ir_block : builder.blocks()) {
    for (auto ir_instr : ir_block->instrs()) {
      ir_instr->set_tag((intptr_t)ordinal++);
    }
  }

  // translate each instruction
  IntInstr *instr_begin = reinterpret_cast<IntInstr *>(codegen_);

  for (auto ir_block : builder.blocks()) {
    for (auto ir_instr : ir_block->instrs()) {
      IntInstr *instr = AllocInstr();
      if (!instr) {
        return nullptr;
      }

      TranslateInstr(*ir_instr, instr);
    }
  }

  IntInstr *instr_end = reinterpret_cast<IntInstr *>(codegen_);
  int num_instr = static_cast<int>(instr_end - instr_begin);
  int guest_cycles = builder.guest_cycles();
  int locals_size = builder.locals_size();

  return std::unique_ptr<RuntimeBlock>(
      new InterpreterBlock(guest_cycles, instr_begin, num_instr, locals_size));
}

uint8_t *InterpreterBackend::Alloc(size_t size) {
  uint8_t *ptr = codegen_;
  codegen_ += size;
  if (codegen_ > codegen_end_) {
    return nullptr;
  }
  return ptr;
}

IntInstr *InterpreterBackend::AllocInstr() {
  IntInstr *instr = reinterpret_cast<IntInstr *>(Alloc(sizeof(IntInstr)));
  if (instr) {
    memset(instr, 0, sizeof(*instr));
  }
  return instr;
}

void InterpreterBackend::TranslateInstr(Instr &ir_i, IntInstr *instr) {
  TranslateArg(ir_i, instr, 0);
  TranslateArg(ir_i, instr, 1);
  TranslateArg(ir_i, instr, 2);
  TranslateArg(ir_i, instr, 3);
  instr->fn = GetCallback(ir_i.op(), GetSignature(ir_i), GetAccessMask(ir_i));
}

void InterpreterBackend::TranslateArg(Instr &ir_i, IntInstr *instr, int arg) {
  Value *ir_v = ir_i.arg(arg);

  if (!ir_v) {
    return;
  }

  IntValue *v = &instr->arg[arg];

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
  } else {
    LOG_FATAL("Unexpected value type");
  }
}
