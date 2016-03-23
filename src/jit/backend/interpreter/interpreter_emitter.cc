#include <math.h>
#include <unordered_map>
#include "core/debug_break.h"
#include "core/memory.h"
#include "hw/memory.h"
#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/interpreter/interpreter_emitter.h"

using namespace re;
using namespace re::hw;
using namespace re::jit;
using namespace re::jit::backend::interpreter;
using namespace re::jit::ir;

static IntFn GetCallback(Instr &ir_i);

InterpreterEmitter::InterpreterEmitter(Memory &memory)
    : memory_(memory), guest_ctx_(nullptr) {
  static const int codegen_size = 1024 * 1024 * 8;
  codegen_begin_ = new uint8_t[codegen_size];
  codegen_end_ = codegen_begin_ + codegen_size;
  codegen_ = codegen_begin_;
}

InterpreterEmitter::~InterpreterEmitter() { delete[] codegen_begin_; }

void InterpreterEmitter::Reset() { codegen_ = codegen_begin_; }

bool InterpreterEmitter::Emit(ir::IRBuilder &builder, void *guest_ctx,
                              IntInstr **instr, int *num_instr,
                              int *locals_size) {
  guest_ctx_ = guest_ctx;

  // do an initial pass assigning ordinals to instructions so local branches
  // can be resolved
  int32_t ordinal = 0;
  for (auto ir_instr : builder.instrs()) {
    ir_instr->set_tag((intptr_t)ordinal++);
  }

  // assign local offsets
  *locals_size = builder.locals_size();

  // translate each instruction
  *instr = reinterpret_cast<IntInstr *>(codegen_);

  for (auto ir_instr : builder.instrs()) {
    IntInstr *instr = AllocInstr();
    if (!instr) {
      return false;
    }

    TranslateInstr(*ir_instr, instr);
  }

  IntInstr *instr_end = reinterpret_cast<IntInstr *>(codegen_);
  *num_instr = static_cast<int>(instr_end - *instr);

  return true;
}

uint8_t *InterpreterEmitter::Alloc(size_t size) {
  uint8_t *ptr = codegen_;
  codegen_ += size;
  if (codegen_ > codegen_end_) {
    return nullptr;
  }
  return ptr;
}

IntInstr *InterpreterEmitter::AllocInstr() {
  IntInstr *instr = reinterpret_cast<IntInstr *>(Alloc(sizeof(IntInstr)));
  if (instr) {
    memset(instr, 0, sizeof(*instr));
  }
  return instr;
}

void InterpreterEmitter::TranslateInstr(Instr &ir_i, IntInstr *instr) {
  // HACK instead of writing out ctx and an array of IntValues, it'd be nice
  // to encode exactly what's needed for each instruction into the codegen
  // buffer
  if (ir_i.op() == OP_LOAD_GUEST || ir_i.op() == OP_STORE_GUEST) {
    instr->ctx = &memory_;
  } else if (ir_i.op() == OP_LOAD_CONTEXT || ir_i.op() == OP_STORE_CONTEXT ||
             ir_i.op() == OP_CALL_EXTERNAL) {
    instr->ctx = guest_ctx_;
  }

  TranslateArg(ir_i, instr, 0);
  TranslateArg(ir_i, instr, 1);
  TranslateArg(ir_i, instr, 2);
  TranslateArg(ir_i, instr, 3);
  instr->fn = GetCallback(ir_i);
}

void InterpreterEmitter::TranslateArg(Instr &ir_i, IntInstr *instr, int arg) {
  Value *ir_v = ir_i.arg(arg);

  if (!ir_v) {
    return;
  }

  IntValue *v = &instr->arg[arg];

  if (ir_v->constant()) {
    switch (ir_v->type()) {
      case VALUE_I8:
        v->i8 = ir_v->i8();
        break;
      case VALUE_I16:
        v->i16 = ir_v->i16();
        break;
      case VALUE_I32:
        v->i32 = ir_v->i32();
        break;
      case VALUE_I64:
        v->i64 = ir_v->i64();
        break;
      case VALUE_F32:
        v->f32 = ir_v->f32();
        break;
      case VALUE_F64:
        v->f64 = ir_v->f64();
        break;
      default:
        LOG_FATAL("Unexpected value type");
        break;
    }
  } else if (ir_v->reg() != NO_REGISTER) {
    v->reg = &int_state.r[ir_v->reg()];
  } else {
    LOG_FATAL("Unexpected value type");
  }
}

// callbacks for each IR operation. callback functions are generated per
// operation, per argument type, per argument access
static std::unordered_map<int, IntFn> int_cbs;

// location in memory of an instruction's argument
enum {
  // argument is located in a virtual register
  ACC_REG = 0x0,
  // argument is encoded as an immediate in the instruction itself
  ACC_IMM = 0x1,
  // 3-bits, 1 for each argument
  NUM_ACC_COMBINATIONS = 1 << 3
};

// OP_SELECT is the only instructions using arg2, and arg2's type always
// matches arg1's. because of this, arg2 isn't considered when generating
// the lookup table
#define MAX_CALLBACKS_PER_OP \
  (VALUE_NUM * VALUE_NUM * VALUE_NUM * NUM_ACC_COMBINATIONS)

#define CALLBACK_IDX(op, r, a0, a1, acc0, acc1, acc2)       \
  (MAX_CALLBACKS_PER_OP * op +                              \
   (((r * VALUE_NUM * VALUE_NUM) + (a0 * VALUE_NUM) + a1) * \
    NUM_ACC_COMBINATIONS) +                                 \
   ((acc2 << 2) | (acc1 << 1) | (acc0 << 0)))

// declare a templated callback for an IR operation. note, declaring a
// callback does not actually register it. callbacks must be registered
// for a particular signature with REGISTER_CALLBACK.
#define INT_CALLBACK(name)                                                     \
  template <typename R = ValueInfo<VALUE_V>, typename A0 = ValueInfo<VALUE_V>, \
            typename A1 = ValueInfo<VALUE_V>, int ACC0, int ACC1, int ACC2>    \
  static void name(const IntInstr *i)

// generate NUM_ACC_COMBINATIONS callbacks for each operation
#define REGISTER_CALLBACK_C(op, fn, r, a0, a1, acc0, acc1, acc2)               \
  int_cbs[CALLBACK_IDX(OP_##op, VALUE_##r, VALUE_##a0, VALUE_##a1, acc0, acc1, \
                       acc2)] =                                                \
      &fn<ValueInfo<VALUE_##r>, ValueInfo<VALUE_##a0>, ValueInfo<VALUE_##a1>,  \
          acc0, acc1, acc2>;

#define REGISTER_INT_CALLBACK(op, fn, r, a0, a1)       \
  static struct _int_##op##_##r##_##a0##_##a1##_init { \
    _int_##op##_##r##_##a0##_##a1##_init() {           \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 0, 0, 0)  \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 0, 0, 1)  \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 0, 1, 0)  \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 0, 1, 1)  \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 1, 0, 0)  \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 1, 0, 1)  \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 1, 1, 0)  \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 1, 1, 1)  \
    }                                                  \
  } int_##op##_##r##_##a0##_##a1##_init

//
// helpers for loading / storing arguments
//
#define LOAD_ARG0() helper<typename A0::signed_type, 0, ACC0>::LoadArg(i)
#define LOAD_ARG1() helper<typename A1::signed_type, 1, ACC1>::LoadArg(i)
#define LOAD_ARG2() helper<typename A1::signed_type, 2, ACC2>::LoadArg(i)
#define LOAD_ARG0_UNSIGNED() \
  static_cast<typename A0::unsigned_type>(LOAD_ARG0())
#define LOAD_ARG1_UNSIGNED() \
  static_cast<typename A1::unsigned_type>(LOAD_ARG1())
#define LOAD_ARG2_UNSIGNED() \
  static_cast<typename A1::unsigned_type>(LOAD_ARG2())
#define STORE_RESULT(v) helper<typename R::signed_type, 3, 0>::StoreArg(i, v)

template <typename T>
static inline T GetValue(const IntValue &v);
template <>
inline int8_t GetValue(const IntValue &v) {
  return v.i8;
}
template <>
inline int16_t GetValue(const IntValue &v) {
  return v.i16;
}
template <>
inline int32_t GetValue(const IntValue &v) {
  return v.i32;
}
template <>
inline int64_t GetValue(const IntValue &v) {
  return v.i64;
}
template <>
inline float GetValue(const IntValue &v) {
  return v.f32;
}
template <>
inline double GetValue(const IntValue &v) {
  return v.f64;
}

template <typename T>
static inline void SetValue(IntValue &v, T n);
template <>
inline void SetValue(IntValue &v, int8_t n) {
  v.i8 = n;
}
template <>
inline void SetValue(IntValue &v, int16_t n) {
  v.i16 = n;
}
template <>
inline void SetValue(IntValue &v, int32_t n) {
  v.i32 = n;
}
template <>
inline void SetValue(IntValue &v, int64_t n) {
  v.i64 = n;
}
template <>
inline void SetValue(IntValue &v, float n) {
  v.f32 = n;
}
template <>
inline void SetValue(IntValue &v, double n) {
  v.f64 = n;
}

template <typename T>
static inline T GetLocal(int offset) {
  return re::load<T>(&int_state.stack[int_state.sp + offset]);
}

template <typename T>
static inline void SetLocal(int offset, T v) {
  re::store(&int_state.stack[int_state.sp + offset], v);
}

template <typename T, int ARG, int ACC>
struct helper {
  static inline T LoadArg(const IntInstr *i);
  static inline void StoreArg(const IntInstr *i, T v);
};

// ACC_REG
// argument is located in a virtual register, arg->i32 specifies the register
template <typename T, int ARG>
struct helper<T, ARG, ACC_REG> {
  static inline T LoadArg(const IntInstr *i) {
    return GetValue<T>(*i->arg[ARG].reg);
  }

  static inline void StoreArg(const IntInstr *i, T v) {
    SetValue<T>(*i->arg[ARG].reg, v);
  }
};

// ACC_IMM
// argument is encoded directly on the instruction
template <typename T, int ARG>
struct helper<T, ARG, ACC_IMM> {
  static inline T LoadArg(const IntInstr *i) {
    return GetValue<T>(i->arg[ARG]);
  }

  static inline void StoreArg(const IntInstr *i, T v) { CHECK(false); }
};

//
// interpreter callbacks
//
INT_CALLBACK(LOAD_HOST_I8) {
  uint64_t addr = LOAD_ARG0();
  auto v = re::load<int8_t>(reinterpret_cast<void *>(addr));
  STORE_RESULT(v);
}
INT_CALLBACK(LOAD_HOST_I16) {
  uint64_t addr = LOAD_ARG0();
  auto v = re::load<int16_t>(reinterpret_cast<void *>(addr));
  STORE_RESULT(v);
}
INT_CALLBACK(LOAD_HOST_I32) {
  uint64_t addr = LOAD_ARG0();
  auto v = re::load<int32_t>(reinterpret_cast<void *>(addr));
  STORE_RESULT(v);
}
INT_CALLBACK(LOAD_HOST_I64) {
  uint64_t addr = LOAD_ARG0();
  auto v = re::load<int64_t>(reinterpret_cast<void *>(addr));
  STORE_RESULT(v);
}
INT_CALLBACK(LOAD_HOST_F32) {
  uint64_t addr = LOAD_ARG0();
  int32_t v = re::load<int32_t>(reinterpret_cast<void *>(addr));
  STORE_RESULT(re::load<float>(&v));
}
INT_CALLBACK(LOAD_HOST_F64) {
  uint64_t addr = LOAD_ARG0();
  int64_t v = re::load<int64_t>(reinterpret_cast<void *>(addr));
  STORE_RESULT(re::load<double>(&v));
}
REGISTER_INT_CALLBACK(LOAD_HOST, LOAD_HOST_I8, I8, I64, V);
REGISTER_INT_CALLBACK(LOAD_HOST, LOAD_HOST_I16, I16, I64, V);
REGISTER_INT_CALLBACK(LOAD_HOST, LOAD_HOST_I32, I32, I64, V);
REGISTER_INT_CALLBACK(LOAD_HOST, LOAD_HOST_I64, I64, I64, V);
REGISTER_INT_CALLBACK(LOAD_HOST, LOAD_HOST_F32, F32, I64, V);
REGISTER_INT_CALLBACK(LOAD_HOST, LOAD_HOST_F64, F64, I64, V);

INT_CALLBACK(STORE_HOST_I8) {
  uint64_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  re::store(reinterpret_cast<void *>(addr), v);
}
INT_CALLBACK(STORE_HOST_I16) {
  uint64_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  re::store(reinterpret_cast<void *>(addr), v);
}
INT_CALLBACK(STORE_HOST_I32) {
  uint64_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  re::store(reinterpret_cast<void *>(addr), v);
}
INT_CALLBACK(STORE_HOST_I64) {
  uint64_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  re::store(reinterpret_cast<void *>(addr), v);
}
INT_CALLBACK(STORE_HOST_F32) {
  uint64_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  re::store(reinterpret_cast<void *>(addr), re::load<uint32_t>(&v));
}
INT_CALLBACK(STORE_HOST_F64) {
  uint64_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  re::store(reinterpret_cast<void *>(addr), re::load<uint64_t>(&v));
}
REGISTER_INT_CALLBACK(STORE_HOST, STORE_HOST_I8, V, I64, I8);
REGISTER_INT_CALLBACK(STORE_HOST, STORE_HOST_I16, V, I64, I16);
REGISTER_INT_CALLBACK(STORE_HOST, STORE_HOST_I32, V, I64, I32);
REGISTER_INT_CALLBACK(STORE_HOST, STORE_HOST_I64, V, I64, I64);
REGISTER_INT_CALLBACK(STORE_HOST, STORE_HOST_F32, V, I64, F32);
REGISTER_INT_CALLBACK(STORE_HOST, STORE_HOST_F64, V, I64, F64);

INT_CALLBACK(LOAD_GUEST_I8) {
  uint32_t addr = LOAD_ARG0();
  auto v = reinterpret_cast<Memory *>(i->ctx)->R8(addr);
  STORE_RESULT(v);
}
INT_CALLBACK(LOAD_GUEST_I16) {
  uint32_t addr = LOAD_ARG0();
  auto v = reinterpret_cast<Memory *>(i->ctx)->R16(addr);
  STORE_RESULT(v);
}
INT_CALLBACK(LOAD_GUEST_I32) {
  uint32_t addr = LOAD_ARG0();
  auto v = reinterpret_cast<Memory *>(i->ctx)->R32(addr);
  STORE_RESULT(v);
}
INT_CALLBACK(LOAD_GUEST_I64) {
  uint32_t addr = LOAD_ARG0();
  auto v = reinterpret_cast<Memory *>(i->ctx)->R64(addr);
  STORE_RESULT(v);
}
INT_CALLBACK(LOAD_GUEST_F32) {
  uint32_t addr = LOAD_ARG0();
  uint32_t v = reinterpret_cast<Memory *>(i->ctx)->R32(addr);
  STORE_RESULT(re::load<float>(&v));
}
INT_CALLBACK(LOAD_GUEST_F64) {
  uint32_t addr = LOAD_ARG0();
  uint64_t v = reinterpret_cast<Memory *>(i->ctx)->R64(addr);
  STORE_RESULT(re::load<double>(&v));
}
REGISTER_INT_CALLBACK(LOAD_GUEST, LOAD_GUEST_I8, I8, I32, V);
REGISTER_INT_CALLBACK(LOAD_GUEST, LOAD_GUEST_I16, I16, I32, V);
REGISTER_INT_CALLBACK(LOAD_GUEST, LOAD_GUEST_I32, I32, I32, V);
REGISTER_INT_CALLBACK(LOAD_GUEST, LOAD_GUEST_I64, I64, I32, V);
REGISTER_INT_CALLBACK(LOAD_GUEST, LOAD_GUEST_F32, F32, I32, V);
REGISTER_INT_CALLBACK(LOAD_GUEST, LOAD_GUEST_F64, F64, I32, V);

INT_CALLBACK(STORE_GUEST_I8) {
  uint32_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  reinterpret_cast<Memory *>(i->ctx)->W8(addr, v);
}
INT_CALLBACK(STORE_GUEST_I16) {
  uint32_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  reinterpret_cast<Memory *>(i->ctx)->W16(addr, v);
}
INT_CALLBACK(STORE_GUEST_I32) {
  uint32_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  reinterpret_cast<Memory *>(i->ctx)->W32(addr, v);
}
INT_CALLBACK(STORE_GUEST_I64) {
  uint32_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  reinterpret_cast<Memory *>(i->ctx)->W64(addr, v);
}
INT_CALLBACK(STORE_GUEST_F32) {
  uint32_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  reinterpret_cast<Memory *>(i->ctx)->W32(addr, re::load<uint32_t>(&v));
}
INT_CALLBACK(STORE_GUEST_F64) {
  uint32_t addr = LOAD_ARG0();
  auto v = LOAD_ARG1();
  reinterpret_cast<Memory *>(i->ctx)->W64(addr, re::load<uint64_t>(&v));
}
REGISTER_INT_CALLBACK(STORE_GUEST, STORE_GUEST_I8, V, I32, I8);
REGISTER_INT_CALLBACK(STORE_GUEST, STORE_GUEST_I16, V, I32, I16);
REGISTER_INT_CALLBACK(STORE_GUEST, STORE_GUEST_I32, V, I32, I32);
REGISTER_INT_CALLBACK(STORE_GUEST, STORE_GUEST_I64, V, I32, I64);
REGISTER_INT_CALLBACK(STORE_GUEST, STORE_GUEST_F32, V, I32, F32);
REGISTER_INT_CALLBACK(STORE_GUEST, STORE_GUEST_F64, V, I32, F64);

INT_CALLBACK(LOAD_CONTEXT) {
  auto offset = LOAD_ARG0();
  auto v = re::load<typename R::signed_type>(
      reinterpret_cast<uint8_t *>(i->ctx) + offset);
  STORE_RESULT(v);
}
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, I8, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, I16, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, I32, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, I64, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, F32, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, F64, I32, V);

INT_CALLBACK(STORE_CONTEXT) {
  auto offset = LOAD_ARG0();
  auto v = LOAD_ARG1();
  re::store(reinterpret_cast<uint8_t *>(i->ctx) + offset, v);
}
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, I8);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, I16);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, I32);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, I64);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, F32);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, F64);

INT_CALLBACK(LOAD_LOCAL) {
  auto offset = LOAD_ARG0();
  auto v = GetLocal<typename R::signed_type>(offset);
  STORE_RESULT(v);
}
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, I8, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, I16, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, I32, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, I64, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, F32, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, F64, I32, V);

INT_CALLBACK(STORE_LOCAL) {
  auto offset = LOAD_ARG0();
  auto v = LOAD_ARG1();
  SetLocal(offset, v);
}
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, I8);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, I16);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, I32);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, I64);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, F32);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, F64);

INT_CALLBACK(BITCAST) {
  auto v = LOAD_ARG0();
  STORE_RESULT(*reinterpret_cast<typename R::signed_type *>(&v));
}
REGISTER_INT_CALLBACK(BITCAST, BITCAST, I8, I16, V);
REGISTER_INT_CALLBACK(BITCAST, BITCAST, I8, I32, V);
REGISTER_INT_CALLBACK(BITCAST, BITCAST, I16, I32, V);
REGISTER_INT_CALLBACK(BITCAST, BITCAST, I8, I64, V);
REGISTER_INT_CALLBACK(BITCAST, BITCAST, I16, I64, V);
REGISTER_INT_CALLBACK(BITCAST, BITCAST, I32, I64, V);

INT_CALLBACK(CAST) {
  auto v = LOAD_ARG0();
  STORE_RESULT(static_cast<typename R::signed_type>(v));
}
REGISTER_INT_CALLBACK(CAST, CAST, F32, I32, V);
REGISTER_INT_CALLBACK(CAST, CAST, F64, I32, V);
REGISTER_INT_CALLBACK(CAST, CAST, F64, I64, V);
REGISTER_INT_CALLBACK(CAST, CAST, I32, F32, V);
REGISTER_INT_CALLBACK(CAST, CAST, I64, F64, V);

INT_CALLBACK(SEXT) {
  auto v = LOAD_ARG0();
  STORE_RESULT(static_cast<typename R::signed_type>(v));
}
REGISTER_INT_CALLBACK(SEXT, SEXT, I16, I8, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I32, I8, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I64, I8, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I32, I16, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I64, I16, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I64, I32, V);

INT_CALLBACK(ZEXT) {
  auto v = LOAD_ARG0_UNSIGNED();
  STORE_RESULT(static_cast<typename R::signed_type>(v));
}
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I16, I8, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I32, I8, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I64, I8, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I32, I16, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I64, I16, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I64, I32, V);

INT_CALLBACK(SELECT) {
  auto cond = LOAD_ARG0();
  auto t = LOAD_ARG1();
  auto f = LOAD_ARG2();
  STORE_RESULT(cond ? t : f);
}
REGISTER_INT_CALLBACK(SELECT, SELECT, I8, I8, I8);
REGISTER_INT_CALLBACK(SELECT, SELECT, I16, I8, I16);
REGISTER_INT_CALLBACK(SELECT, SELECT, I32, I8, I32);
REGISTER_INT_CALLBACK(SELECT, SELECT, I64, I8, I64);

INT_CALLBACK(EQ) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs == rhs));
}
REGISTER_INT_CALLBACK(EQ, EQ, I8, I8, I8);
REGISTER_INT_CALLBACK(EQ, EQ, I8, I16, I16);
REGISTER_INT_CALLBACK(EQ, EQ, I8, I32, I32);
REGISTER_INT_CALLBACK(EQ, EQ, I8, I64, I64);
REGISTER_INT_CALLBACK(EQ, EQ, I8, F32, F32);
REGISTER_INT_CALLBACK(EQ, EQ, I8, F64, F64);

INT_CALLBACK(NE) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs != rhs));
}
REGISTER_INT_CALLBACK(NE, NE, I8, I8, I8);
REGISTER_INT_CALLBACK(NE, NE, I8, I16, I16);
REGISTER_INT_CALLBACK(NE, NE, I8, I32, I32);
REGISTER_INT_CALLBACK(NE, NE, I8, I64, I64);
REGISTER_INT_CALLBACK(NE, NE, I8, F32, F32);
REGISTER_INT_CALLBACK(NE, NE, I8, F64, F64);

INT_CALLBACK(SGE) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs >= rhs));
}
REGISTER_INT_CALLBACK(SGE, SGE, I8, I8, I8);
REGISTER_INT_CALLBACK(SGE, SGE, I8, I16, I16);
REGISTER_INT_CALLBACK(SGE, SGE, I8, I32, I32);
REGISTER_INT_CALLBACK(SGE, SGE, I8, I64, I64);
REGISTER_INT_CALLBACK(SGE, SGE, I8, F32, F32);
REGISTER_INT_CALLBACK(SGE, SGE, I8, F64, F64);

INT_CALLBACK(SGT) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs > rhs));
}
REGISTER_INT_CALLBACK(SGT, SGT, I8, I8, I8);
REGISTER_INT_CALLBACK(SGT, SGT, I8, I16, I16);
REGISTER_INT_CALLBACK(SGT, SGT, I8, I32, I32);
REGISTER_INT_CALLBACK(SGT, SGT, I8, I64, I64);
REGISTER_INT_CALLBACK(SGT, SGT, I8, F32, F32);
REGISTER_INT_CALLBACK(SGT, SGT, I8, F64, F64);

INT_CALLBACK(UGE) {
  auto lhs = LOAD_ARG0_UNSIGNED();
  auto rhs = LOAD_ARG1_UNSIGNED();
  STORE_RESULT(static_cast<int8_t>(lhs >= rhs));
}
REGISTER_INT_CALLBACK(UGE, UGE, I8, I8, I8);
REGISTER_INT_CALLBACK(UGE, UGE, I8, I16, I16);
REGISTER_INT_CALLBACK(UGE, UGE, I8, I32, I32);
REGISTER_INT_CALLBACK(UGE, UGE, I8, I64, I64);

INT_CALLBACK(UGT) {
  auto lhs = LOAD_ARG0_UNSIGNED();
  auto rhs = LOAD_ARG1_UNSIGNED();
  STORE_RESULT(static_cast<int8_t>(lhs > rhs));
}
REGISTER_INT_CALLBACK(UGT, UGT, I8, I8, I8);
REGISTER_INT_CALLBACK(UGT, UGT, I8, I16, I16);
REGISTER_INT_CALLBACK(UGT, UGT, I8, I32, I32);
REGISTER_INT_CALLBACK(UGT, UGT, I8, I64, I64);

INT_CALLBACK(SLE) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs <= rhs));
}
REGISTER_INT_CALLBACK(SLE, SLE, I8, I8, I8);
REGISTER_INT_CALLBACK(SLE, SLE, I8, I16, I16);
REGISTER_INT_CALLBACK(SLE, SLE, I8, I32, I32);
REGISTER_INT_CALLBACK(SLE, SLE, I8, I64, I64);
REGISTER_INT_CALLBACK(SLE, SLE, I8, F32, F32);
REGISTER_INT_CALLBACK(SLE, SLE, I8, F64, F64);

INT_CALLBACK(SLT) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs < rhs));
}
REGISTER_INT_CALLBACK(SLT, SLT, I8, I8, I8);
REGISTER_INT_CALLBACK(SLT, SLT, I8, I16, I16);
REGISTER_INT_CALLBACK(SLT, SLT, I8, I32, I32);
REGISTER_INT_CALLBACK(SLT, SLT, I8, I64, I64);
REGISTER_INT_CALLBACK(SLT, SLT, I8, F32, F32);
REGISTER_INT_CALLBACK(SLT, SLT, I8, F64, F64);

INT_CALLBACK(ULE) {
  auto lhs = LOAD_ARG0_UNSIGNED();
  auto rhs = LOAD_ARG1_UNSIGNED();
  STORE_RESULT(static_cast<int8_t>(lhs <= rhs));
}
REGISTER_INT_CALLBACK(ULE, ULE, I8, I8, I8);
REGISTER_INT_CALLBACK(ULE, ULE, I8, I16, I16);
REGISTER_INT_CALLBACK(ULE, ULE, I8, I32, I32);
REGISTER_INT_CALLBACK(ULE, ULE, I8, I64, I64);

INT_CALLBACK(ULT) {
  auto lhs = LOAD_ARG0_UNSIGNED();
  auto rhs = LOAD_ARG1_UNSIGNED();
  STORE_RESULT(static_cast<int8_t>(lhs < rhs));
}
REGISTER_INT_CALLBACK(ULT, ULT, I8, I8, I8);
REGISTER_INT_CALLBACK(ULT, ULT, I8, I16, I16);
REGISTER_INT_CALLBACK(ULT, ULT, I8, I32, I32);
REGISTER_INT_CALLBACK(ULT, ULT, I8, I64, I64);

INT_CALLBACK(ADD) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(lhs + rhs);
}
REGISTER_INT_CALLBACK(ADD, ADD, I8, I8, I8);
REGISTER_INT_CALLBACK(ADD, ADD, I16, I16, I16);
REGISTER_INT_CALLBACK(ADD, ADD, I32, I32, I32);
REGISTER_INT_CALLBACK(ADD, ADD, I64, I64, I64);
REGISTER_INT_CALLBACK(ADD, ADD, F32, F32, F32);
REGISTER_INT_CALLBACK(ADD, ADD, F64, F64, F64);

INT_CALLBACK(SUB) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(lhs - rhs);
}
REGISTER_INT_CALLBACK(SUB, SUB, I8, I8, I8);
REGISTER_INT_CALLBACK(SUB, SUB, I16, I16, I16);
REGISTER_INT_CALLBACK(SUB, SUB, I32, I32, I32);
REGISTER_INT_CALLBACK(SUB, SUB, I64, I64, I64);
REGISTER_INT_CALLBACK(SUB, SUB, F32, F32, F32);
REGISTER_INT_CALLBACK(SUB, SUB, F64, F64, F64);

INT_CALLBACK(SMUL) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(lhs * rhs);
}
REGISTER_INT_CALLBACK(SMUL, SMUL, I8, I8, I8);
REGISTER_INT_CALLBACK(SMUL, SMUL, I16, I16, I16);
REGISTER_INT_CALLBACK(SMUL, SMUL, I32, I32, I32);
REGISTER_INT_CALLBACK(SMUL, SMUL, I64, I64, I64);
REGISTER_INT_CALLBACK(SMUL, SMUL, F32, F32, F32);
REGISTER_INT_CALLBACK(SMUL, SMUL, F64, F64, F64);

INT_CALLBACK(UMUL) {
  auto lhs = LOAD_ARG0_UNSIGNED();
  auto rhs = LOAD_ARG1_UNSIGNED();
  STORE_RESULT(static_cast<typename R::signed_type>(lhs * rhs));
}
REGISTER_INT_CALLBACK(UMUL, UMUL, I8, I8, I8);
REGISTER_INT_CALLBACK(UMUL, UMUL, I16, I16, I16);
REGISTER_INT_CALLBACK(UMUL, UMUL, I32, I32, I32);
REGISTER_INT_CALLBACK(UMUL, UMUL, I64, I64, I64);

INT_CALLBACK(DIV) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(lhs / rhs);
}
REGISTER_INT_CALLBACK(DIV, DIV, I8, I8, I8);
REGISTER_INT_CALLBACK(DIV, DIV, I16, I16, I16);
REGISTER_INT_CALLBACK(DIV, DIV, I32, I32, I32);
REGISTER_INT_CALLBACK(DIV, DIV, I64, I64, I64);
REGISTER_INT_CALLBACK(DIV, DIV, F32, F32, F32);
REGISTER_INT_CALLBACK(DIV, DIV, F64, F64, F64);

INT_CALLBACK(NEG) {
  auto v = LOAD_ARG0();
  STORE_RESULT(-v);
}
REGISTER_INT_CALLBACK(NEG, NEG, I8, I8, V);
REGISTER_INT_CALLBACK(NEG, NEG, I16, I16, V);
REGISTER_INT_CALLBACK(NEG, NEG, I32, I32, V);
REGISTER_INT_CALLBACK(NEG, NEG, I64, I64, V);
REGISTER_INT_CALLBACK(NEG, NEG, F32, F32, V);
REGISTER_INT_CALLBACK(NEG, NEG, F64, F64, V);

INT_CALLBACK(SQRTF) {
  auto v = LOAD_ARG0();
  STORE_RESULT(sqrtf(v));
}
INT_CALLBACK(SQRT) {
  auto v = LOAD_ARG0();
  STORE_RESULT(sqrt(v));
}
REGISTER_INT_CALLBACK(SQRT, SQRTF, F32, F32, V);
REGISTER_INT_CALLBACK(SQRT, SQRT, F64, F64, V);

INT_CALLBACK(ABSF) {
  auto v = LOAD_ARG0();
  STORE_RESULT(fabs(v));
}
REGISTER_INT_CALLBACK(ABS, ABSF, F32, F32, V);
REGISTER_INT_CALLBACK(ABS, ABSF, F64, F64, V);

INT_CALLBACK(AND) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(lhs & rhs);
}
REGISTER_INT_CALLBACK(AND, AND, I8, I8, I8);
REGISTER_INT_CALLBACK(AND, AND, I16, I16, I16);
REGISTER_INT_CALLBACK(AND, AND, I32, I32, I32);
REGISTER_INT_CALLBACK(AND, AND, I64, I64, I64);

INT_CALLBACK(OR) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(lhs | rhs);
}
REGISTER_INT_CALLBACK(OR, OR, I8, I8, I8);
REGISTER_INT_CALLBACK(OR, OR, I16, I16, I16);
REGISTER_INT_CALLBACK(OR, OR, I32, I32, I32);
REGISTER_INT_CALLBACK(OR, OR, I64, I64, I64);

INT_CALLBACK(XOR) {
  auto lhs = LOAD_ARG0();
  auto rhs = LOAD_ARG1();
  STORE_RESULT(lhs ^ rhs);
}
REGISTER_INT_CALLBACK(XOR, XOR, I8, I8, I8);
REGISTER_INT_CALLBACK(XOR, XOR, I16, I16, I16);
REGISTER_INT_CALLBACK(XOR, XOR, I32, I32, I32);
REGISTER_INT_CALLBACK(XOR, XOR, I64, I64, I64);

INT_CALLBACK(NOT) {
  auto v = LOAD_ARG0();
  STORE_RESULT(~v);
}
REGISTER_INT_CALLBACK(NOT, NOT, I8, I8, V);
REGISTER_INT_CALLBACK(NOT, NOT, I16, I16, V);
REGISTER_INT_CALLBACK(NOT, NOT, I32, I32, V);
REGISTER_INT_CALLBACK(NOT, NOT, I64, I64, V);

INT_CALLBACK(SHL) {
  auto v = LOAD_ARG0();
  auto n = LOAD_ARG1();
  STORE_RESULT(v << n);
}
REGISTER_INT_CALLBACK(SHL, SHL, I8, I8, I32);
REGISTER_INT_CALLBACK(SHL, SHL, I16, I16, I32);
REGISTER_INT_CALLBACK(SHL, SHL, I32, I32, I32);
REGISTER_INT_CALLBACK(SHL, SHL, I64, I64, I32);

INT_CALLBACK(ASHR) {
  auto v = LOAD_ARG0();
  auto n = LOAD_ARG1();
  STORE_RESULT(v >> n);
}
REGISTER_INT_CALLBACK(ASHR, ASHR, I8, I8, I32);
REGISTER_INT_CALLBACK(ASHR, ASHR, I16, I16, I32);
REGISTER_INT_CALLBACK(ASHR, ASHR, I32, I32, I32);
REGISTER_INT_CALLBACK(ASHR, ASHR, I64, I64, I32);

INT_CALLBACK(LSHR) {
  auto v = LOAD_ARG0_UNSIGNED();
  auto n = LOAD_ARG1();
  STORE_RESULT(static_cast<typename R::signed_type>(v >> n));
}
REGISTER_INT_CALLBACK(LSHR, LSHR, I8, I8, I32);
REGISTER_INT_CALLBACK(LSHR, LSHR, I16, I16, I32);
REGISTER_INT_CALLBACK(LSHR, LSHR, I32, I32, I32);
REGISTER_INT_CALLBACK(LSHR, LSHR, I64, I64, I32);

INT_CALLBACK(ASHD) {
  int32_t v = LOAD_ARG0();
  int32_t n = LOAD_ARG1();
  if (n & 0x80000000) {
    if (n & 0x1f) {
      v = v >> -(n & 0x1f);
    } else {
      v = v >> 31;
    }
  } else {
    v = v << (n & 0x1f);
  }
  STORE_RESULT(v);
}
REGISTER_INT_CALLBACK(ASHD, ASHD, I32, I32, I32);

INT_CALLBACK(LSHD) {
  uint32_t v = static_cast<uint32_t>(LOAD_ARG0());
  int32_t n = LOAD_ARG1();
  if (n & 0x80000000) {
    if (n & 0x1f) {
      v = v >> -(n & 0x1f);
    } else {
      v = 0;
    }
  } else {
    v = v << (n & 0x1f);
  }
  STORE_RESULT(v);
}
REGISTER_INT_CALLBACK(LSHD, LSHD, I32, I32, I32);

INT_CALLBACK(CALL_EXTERNAL1) {
  auto addr = LOAD_ARG0();
  void (*func)(void *) = reinterpret_cast<void (*)(void *)>(addr);
  func(i->ctx);
}
INT_CALLBACK(CALL_EXTERNAL2) {
  auto addr = LOAD_ARG0();
  auto arg = LOAD_ARG1();
  void (*func)(void *, uint64_t) =
      reinterpret_cast<void (*)(void *, uint64_t)>(addr);
  func(i->ctx, arg);
}
REGISTER_INT_CALLBACK(CALL_EXTERNAL, CALL_EXTERNAL1, V, I64, V);
REGISTER_INT_CALLBACK(CALL_EXTERNAL, CALL_EXTERNAL2, V, I64, I64);

//
// lookup callback for ir instruction
//
static int GetArgType(Instr &ir_i, int arg) {
  Value *ir_v = ir_i.arg(arg);
  if (!ir_v) {
    return 0;
  }

  return ir_v->type();
}

static int GetArgAccess(Instr &ir_i, int arg) {
  Value *ir_v = ir_i.arg(arg);
  if (!ir_v) {
    return 0;
  }

  int access = ACC_REG;
  if (ir_v->constant()) {
    access = ACC_IMM;
  }
  return access;
}

static IntFn GetCallback(Instr &ir_i) {
  Op op = ir_i.op();
  auto it = int_cbs.find(CALLBACK_IDX(
      op, GetArgType(ir_i, 3), GetArgType(ir_i, 0), GetArgType(ir_i, 1),
      GetArgAccess(ir_i, 0), GetArgAccess(ir_i, 1), GetArgAccess(ir_i, 2)));
  CHECK_NE(it, int_cbs.end(), "Failed to lookup callback for %s", Opnames[op]);
  return it->second;
}
