#include <unordered_map>
#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/interpreter/interpreter_block.h"
#include "jit/backend/interpreter/interpreter_callbacks.h"

using namespace dreavm::core;
using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend::interpreter;
using namespace dreavm::jit::ir;

// callbacks for each IR operation. callback functions are generated per
// operation, per signature, per access mask.
static std::unordered_map<int, IntFn> int_cbs;

// OP_SELECT and OP_BRANCH_COND are the only instructions using arg2, and
// arg2's type always matches arg1's. because of this, arg2 isn't considered
// when generating the lookup table.
#define MAX_CALLBACKS_PER_OP \
  (VALUE_NUM * VALUE_NUM * VALUE_NUM * NUM_ACC_COMBINATIONS)

#define CALLBACK_IDX(op, result_sig, arg0_sig, arg1_sig, access_mask)   \
  (MAX_CALLBACKS_PER_OP * op + (((result_sig * VALUE_NUM * VALUE_NUM) + \
                                 (arg0_sig * VALUE_NUM) + arg1_sig) *   \
                                NUM_ACC_COMBINATIONS) +                 \
   access_mask)

// declare a templated callback for an IR operation. note, declaring a
// callback does not actually register it. callbacks must be registered
// for a particular signature with REGISTER_CALLBACK.
#define INT_CALLBACK(name)                                              \
  template <typename R = void, typename A0 = void, typename A1 = void,  \
            int ACCESS_MASK = 0>                                        \
  static uint32_t name(const IntInstr *i, uint32_t idx, Memory *memory, \
                       IntValue *r, uint8_t *locals, void *guest_ctx)

// generate NUM_ACC_COMBINATIONS callbacks for each operation
#define REGISTER_CALLBACK_C(op, fn, r, a0, a1, c)                        \
  int_cbs[CALLBACK_IDX(OP_##op, VALUE_##r, VALUE_##a0, VALUE_##a1, c)] = \
      &fn<ValueType<VALUE_##r>::type, ValueType<VALUE_##a0>::type,       \
          ValueType<VALUE_##a1>::type, c>;

#define REGISTER_INT_CALLBACK(op, fn, r, a0, a1)       \
  static struct _int_##op##_##r##_##a0##_##a1##_init { \
    _int_##op##_##r##_##a0##_##a1##_init() {           \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 0)        \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 1)        \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 2)        \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 3)        \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 4)        \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 5)        \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 6)        \
      REGISTER_CALLBACK_C(op, fn, r, a0, a1, 7)        \
    }                                                  \
  } int_##op##_##r##_##a0##_##a1##_init

IntFn dreavm::jit::backend::interpreter::GetCallback(
    Opcode op, const IntSig &sig, IntAccessMask access_mask) {
  auto it = int_cbs.find(CALLBACK_IDX(op, GetArgSignature(sig, 3),
                                      GetArgSignature(sig, 0),
                                      GetArgSignature(sig, 1), access_mask));
  CHECK_NE(it, int_cbs.end(), "Failed to lookup callback for %s", Opnames[op]);
  return it->second;
}

//
// helpers for loading / storing arguments
//
#define LOAD_ARG0() helper<A0, 0, ACCESS_MASK>::LoadArg(i, r, locals)
#define LOAD_ARG1() helper<A1, 1, ACCESS_MASK>::LoadArg(i, r, locals)
#define LOAD_ARG2() helper<A1, 2, ACCESS_MASK>::LoadArg(i, r, locals)
#define STORE_RESULT(v) helper<R, 3, ACCESS_MASK>::StoreArg(i, r, locals, v)
#define NEXT_INSTR (idx + 1)

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
static inline T GetLocal(const uint8_t *locals, int offset);
template <>
inline int8_t GetLocal(const uint8_t *locals, int offset) {
  return *reinterpret_cast<const int8_t *>(&locals[offset]);
}
template <>
inline int16_t GetLocal(const uint8_t *locals, int offset) {
  return *reinterpret_cast<const int16_t *>(&locals[offset]);
}
template <>
inline int32_t GetLocal(const uint8_t *locals, int offset) {
  return *reinterpret_cast<const int32_t *>(&locals[offset]);
}
template <>
inline int64_t GetLocal(const uint8_t *locals, int offset) {
  return *reinterpret_cast<const int64_t *>(&locals[offset]);
}
template <>
inline float GetLocal(const uint8_t *locals, int offset) {
  return *reinterpret_cast<const float *>(&locals[offset]);
}
template <>
inline double GetLocal(const uint8_t *locals, int offset) {
  return *reinterpret_cast<const double *>(&locals[offset]);
}

template <typename T>
static inline void SetLocal(uint8_t *locals, int offset, T v);
template <>
inline void SetLocal(uint8_t *locals, int offset, int8_t v) {
  *reinterpret_cast<int8_t *>(&locals[offset]) = v;
}
template <>
inline void SetLocal(uint8_t *locals, int offset, int16_t v) {
  *reinterpret_cast<int16_t *>(&locals[offset]) = v;
}
template <>
inline void SetLocal(uint8_t *locals, int offset, int32_t v) {
  *reinterpret_cast<int32_t *>(&locals[offset]) = v;
}
template <>
inline void SetLocal(uint8_t *locals, int offset, int64_t v) {
  *reinterpret_cast<int64_t *>(&locals[offset]) = v;
}
template <>
inline void SetLocal(uint8_t *locals, int offset, float v) {
  *reinterpret_cast<float *>(&locals[offset]) = v;
}
template <>
inline void SetLocal(uint8_t *locals, int offset, double v) {
  *reinterpret_cast<double *>(&locals[offset]) = v;
}

template <typename T, int ARG, IntAccessMask ACCESS_MASK, class ENABLE = void>
struct helper {
  static inline T LoadArg(const IntInstr *i, const IntValue *r,
                          const uint8_t *l);
  static inline void StoreArg(const IntInstr *i, const IntValue *r,
                              const uint8_t *l, T v);
};

// ACC_REG
// argument is located in a virtual register, arg->i32 specifies the register
template <typename T, int ARG, IntAccessMask ACCESS_MASK>
struct helper<
    T, ARG, ACCESS_MASK,
    typename std::enable_if<GetArgAccess(ACCESS_MASK, ARG) == ACC_REG>::type> {
  static inline T LoadArg(const IntInstr *i, const IntValue *r,
                          const uint8_t *l) {
    return GetValue<T>(r[i->arg[ARG].i32]);
  }

  static inline void StoreArg(const IntInstr *i, IntValue *r, uint8_t *l, T v) {
    SetValue<T>(r[i->arg[ARG].i32], v);
  }
};

// ACC_IMM
// argument is encoded directly on the instruction
template <typename T, int ARG, IntAccessMask ACCESS_MASK>
struct helper<
    T, ARG, ACCESS_MASK,
    typename std::enable_if<GetArgAccess(ACCESS_MASK, ARG) == ACC_IMM>::type> {
  static inline T LoadArg(const IntInstr *i, const IntValue *r,
                          const uint8_t *l) {
    return GetValue<T>(i->arg[ARG]);
  }

  static inline void StoreArg(const IntInstr *i, IntValue *r, uint8_t *l, T v) {
    CHECK(false);
  }
};

//
// interpreter callbacks
//
INT_CALLBACK(LOAD_CONTEXT) {
  A0 offset = LOAD_ARG0();
  R v = *reinterpret_cast<R *>(reinterpret_cast<uint8_t *>(guest_ctx) + offset);
  STORE_RESULT(v);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, I8, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, I16, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, I32, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, I64, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, F32, I32, V);
REGISTER_INT_CALLBACK(LOAD_CONTEXT, LOAD_CONTEXT, F64, I32, V);

INT_CALLBACK(STORE_CONTEXT) {
  A0 offset = LOAD_ARG0();
  A1 v = LOAD_ARG1();
  *reinterpret_cast<A1 *>(reinterpret_cast<uint8_t *>(guest_ctx) + offset) = v;
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, I8);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, I16);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, I32);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, I64);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, F32);
REGISTER_INT_CALLBACK(STORE_CONTEXT, STORE_CONTEXT, V, I32, F64);

INT_CALLBACK(LOAD_LOCAL) {
  A0 offset = LOAD_ARG0();
  R v = GetLocal<R>(locals, offset);
  STORE_RESULT(v);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, I8, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, I16, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, I32, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, I64, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, F32, I32, V);
REGISTER_INT_CALLBACK(LOAD_LOCAL, LOAD_LOCAL, F64, I32, V);

INT_CALLBACK(STORE_LOCAL) {
  A0 offset = LOAD_ARG0();
  A1 v = LOAD_ARG1();
  SetLocal(locals, offset, v);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, I8);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, I16);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, I32);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, I64);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, F32);
REGISTER_INT_CALLBACK(STORE_LOCAL, STORE_LOCAL, V, I32, F64);

INT_CALLBACK(LOAD_I8) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->R8(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}
INT_CALLBACK(LOAD_I16) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->R16(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}
INT_CALLBACK(LOAD_I32) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->R32(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}
INT_CALLBACK(LOAD_I64) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->R64(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}
INT_CALLBACK(LOAD_F32) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  uint32_t v = memory->R32(addr);
  STORE_RESULT(*reinterpret_cast<float *>(&v));
  return NEXT_INSTR;
}
INT_CALLBACK(LOAD_F64) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  uint64_t v = memory->R64(addr);
  STORE_RESULT(*reinterpret_cast<double *>(&v));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(LOAD, LOAD_I8, I8, I32, V);
REGISTER_INT_CALLBACK(LOAD, LOAD_I16, I16, I32, V);
REGISTER_INT_CALLBACK(LOAD, LOAD_I32, I32, I32, V);
REGISTER_INT_CALLBACK(LOAD, LOAD_I64, I64, I32, V);
REGISTER_INT_CALLBACK(LOAD, LOAD_F32, F32, I32, V);
REGISTER_INT_CALLBACK(LOAD, LOAD_F64, F64, I32, V);

INT_CALLBACK(STORE_I8) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W8(addr, v);
  return NEXT_INSTR;
}
INT_CALLBACK(STORE_I16) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W16(addr, v);
  return NEXT_INSTR;
}
INT_CALLBACK(STORE_I32) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W32(addr, v);
  return NEXT_INSTR;
}
INT_CALLBACK(STORE_I64) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W64(addr, v);
  return NEXT_INSTR;
}
INT_CALLBACK(STORE_F32) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W32(addr, *reinterpret_cast<uint32_t *>(&v));
  return NEXT_INSTR;
}
INT_CALLBACK(STORE_F64) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W64(addr, *reinterpret_cast<uint64_t *>(&v));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(STORE, STORE_I8, V, I32, I8);
REGISTER_INT_CALLBACK(STORE, STORE_I16, V, I32, I16);
REGISTER_INT_CALLBACK(STORE, STORE_I32, V, I32, I32);
REGISTER_INT_CALLBACK(STORE, STORE_I64, V, I32, I64);
REGISTER_INT_CALLBACK(STORE, STORE_F32, V, I32, F32);
REGISTER_INT_CALLBACK(STORE, STORE_F64, V, I32, F64);

INT_CALLBACK(CAST) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(static_cast<R>(v));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(CAST, CAST, F32, I32, V);
REGISTER_INT_CALLBACK(CAST, CAST, F64, I32, V);
REGISTER_INT_CALLBACK(CAST, CAST, F64, I64, V);
REGISTER_INT_CALLBACK(CAST, CAST, I32, F32, V);
REGISTER_INT_CALLBACK(CAST, CAST, I64, F64, V);

INT_CALLBACK(SEXT) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(static_cast<R>(v));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SEXT, SEXT, I16, I8, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I32, I8, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I64, I8, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I32, I16, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I64, I16, V);
REGISTER_INT_CALLBACK(SEXT, SEXT, I64, I32, V);

INT_CALLBACK(ZEXT) {
  using U0 = typename std::make_unsigned<A0>::type;
  A0 v = LOAD_ARG0();
  STORE_RESULT(static_cast<R>(static_cast<U0>(v)));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I16, I8, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I32, I8, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I64, I8, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I32, I16, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I64, I16, V);
REGISTER_INT_CALLBACK(ZEXT, ZEXT, I64, I32, V);

INT_CALLBACK(TRUNCATE) {
  using U0 = typename std::make_unsigned<A0>::type;
  A0 v = LOAD_ARG0();
  STORE_RESULT(static_cast<R>(static_cast<U0>(v)));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(TRUNCATE, TRUNCATE, I8, I16, V);
REGISTER_INT_CALLBACK(TRUNCATE, TRUNCATE, I8, I32, V);
REGISTER_INT_CALLBACK(TRUNCATE, TRUNCATE, I16, I32, V);
REGISTER_INT_CALLBACK(TRUNCATE, TRUNCATE, I8, I64, V);
REGISTER_INT_CALLBACK(TRUNCATE, TRUNCATE, I16, I64, V);
REGISTER_INT_CALLBACK(TRUNCATE, TRUNCATE, I32, I64, V);

INT_CALLBACK(SELECT) {
  A0 cond = LOAD_ARG0();
  A1 t = LOAD_ARG1();
  A1 f = LOAD_ARG2();
  STORE_RESULT(cond ? t : f);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SELECT, SELECT, I8, I8, I8);
REGISTER_INT_CALLBACK(SELECT, SELECT, I16, I8, I16);
REGISTER_INT_CALLBACK(SELECT, SELECT, I32, I8, I32);
REGISTER_INT_CALLBACK(SELECT, SELECT, I64, I8, I64);

INT_CALLBACK(EQ) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs == rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(EQ, EQ, I8, I8, I8);
REGISTER_INT_CALLBACK(EQ, EQ, I8, I16, I16);
REGISTER_INT_CALLBACK(EQ, EQ, I8, I32, I32);
REGISTER_INT_CALLBACK(EQ, EQ, I8, I64, I64);
REGISTER_INT_CALLBACK(EQ, EQ, I8, F32, F32);
REGISTER_INT_CALLBACK(EQ, EQ, I8, F64, F64);

INT_CALLBACK(NE) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs != rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(NE, NE, I8, I8, I8);
REGISTER_INT_CALLBACK(NE, NE, I8, I16, I16);
REGISTER_INT_CALLBACK(NE, NE, I8, I32, I32);
REGISTER_INT_CALLBACK(NE, NE, I8, I64, I64);
REGISTER_INT_CALLBACK(NE, NE, I8, F32, F32);
REGISTER_INT_CALLBACK(NE, NE, I8, F64, F64);

INT_CALLBACK(SGE) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs >= rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SGE, SGE, I8, I8, I8);
REGISTER_INT_CALLBACK(SGE, SGE, I8, I16, I16);
REGISTER_INT_CALLBACK(SGE, SGE, I8, I32, I32);
REGISTER_INT_CALLBACK(SGE, SGE, I8, I64, I64);
REGISTER_INT_CALLBACK(SGE, SGE, I8, F32, F32);
REGISTER_INT_CALLBACK(SGE, SGE, I8, F64, F64);

INT_CALLBACK(SGT) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs > rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SGT, SGT, I8, I8, I8);
REGISTER_INT_CALLBACK(SGT, SGT, I8, I16, I16);
REGISTER_INT_CALLBACK(SGT, SGT, I8, I32, I32);
REGISTER_INT_CALLBACK(SGT, SGT, I8, I64, I64);
REGISTER_INT_CALLBACK(SGT, SGT, I8, F32, F32);
REGISTER_INT_CALLBACK(SGT, SGT, I8, F64, F64);

INT_CALLBACK(UGE) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = static_cast<U0>(LOAD_ARG0());
  U1 rhs = static_cast<U1>(LOAD_ARG1());
  STORE_RESULT(static_cast<int8_t>(lhs >= rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(UGE, UGE, I8, I8, I8);
REGISTER_INT_CALLBACK(UGE, UGE, I8, I16, I16);
REGISTER_INT_CALLBACK(UGE, UGE, I8, I32, I32);
REGISTER_INT_CALLBACK(UGE, UGE, I8, I64, I64);

INT_CALLBACK(UGT) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = static_cast<U0>(LOAD_ARG0());
  U1 rhs = static_cast<U1>(LOAD_ARG1());
  STORE_RESULT(static_cast<int8_t>(lhs > rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(UGT, UGT, I8, I8, I8);
REGISTER_INT_CALLBACK(UGT, UGT, I8, I16, I16);
REGISTER_INT_CALLBACK(UGT, UGT, I8, I32, I32);
REGISTER_INT_CALLBACK(UGT, UGT, I8, I64, I64);

INT_CALLBACK(SLE) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs <= rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SLE, SLE, I8, I8, I8);
REGISTER_INT_CALLBACK(SLE, SLE, I8, I16, I16);
REGISTER_INT_CALLBACK(SLE, SLE, I8, I32, I32);
REGISTER_INT_CALLBACK(SLE, SLE, I8, I64, I64);
REGISTER_INT_CALLBACK(SLE, SLE, I8, F32, F32);
REGISTER_INT_CALLBACK(SLE, SLE, I8, F64, F64);

INT_CALLBACK(SLT) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(static_cast<int8_t>(lhs < rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SLT, SLT, I8, I8, I8);
REGISTER_INT_CALLBACK(SLT, SLT, I8, I16, I16);
REGISTER_INT_CALLBACK(SLT, SLT, I8, I32, I32);
REGISTER_INT_CALLBACK(SLT, SLT, I8, I64, I64);
REGISTER_INT_CALLBACK(SLT, SLT, I8, F32, F32);
REGISTER_INT_CALLBACK(SLT, SLT, I8, F64, F64);

INT_CALLBACK(ULE) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = static_cast<U0>(LOAD_ARG0());
  U1 rhs = static_cast<U1>(LOAD_ARG1());
  STORE_RESULT(static_cast<int8_t>(lhs <= rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(ULE, ULE, I8, I8, I8);
REGISTER_INT_CALLBACK(ULE, ULE, I8, I16, I16);
REGISTER_INT_CALLBACK(ULE, ULE, I8, I32, I32);
REGISTER_INT_CALLBACK(ULE, ULE, I8, I64, I64);

INT_CALLBACK(ULT) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = static_cast<U0>(LOAD_ARG0());
  U1 rhs = static_cast<U1>(LOAD_ARG1());
  STORE_RESULT(static_cast<int8_t>(lhs < rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(ULT, ULT, I8, I8, I8);
REGISTER_INT_CALLBACK(ULT, ULT, I8, I16, I16);
REGISTER_INT_CALLBACK(ULT, ULT, I8, I32, I32);
REGISTER_INT_CALLBACK(ULT, ULT, I8, I64, I64);

INT_CALLBACK(ADD) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs + rhs);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(ADD, ADD, I8, I8, I8);
REGISTER_INT_CALLBACK(ADD, ADD, I16, I16, I16);
REGISTER_INT_CALLBACK(ADD, ADD, I32, I32, I32);
REGISTER_INT_CALLBACK(ADD, ADD, I64, I64, I64);
REGISTER_INT_CALLBACK(ADD, ADD, F32, F32, F32);
REGISTER_INT_CALLBACK(ADD, ADD, F64, F64, F64);

INT_CALLBACK(SUB) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs - rhs);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SUB, SUB, I8, I8, I8);
REGISTER_INT_CALLBACK(SUB, SUB, I16, I16, I16);
REGISTER_INT_CALLBACK(SUB, SUB, I32, I32, I32);
REGISTER_INT_CALLBACK(SUB, SUB, I64, I64, I64);
REGISTER_INT_CALLBACK(SUB, SUB, F32, F32, F32);
REGISTER_INT_CALLBACK(SUB, SUB, F64, F64, F64);

INT_CALLBACK(SMUL) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs * rhs);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SMUL, SMUL, I8, I8, I8);
REGISTER_INT_CALLBACK(SMUL, SMUL, I16, I16, I16);
REGISTER_INT_CALLBACK(SMUL, SMUL, I32, I32, I32);
REGISTER_INT_CALLBACK(SMUL, SMUL, I64, I64, I64);
REGISTER_INT_CALLBACK(SMUL, SMUL, F32, F32, F32);
REGISTER_INT_CALLBACK(SMUL, SMUL, F64, F64, F64);

INT_CALLBACK(UMUL) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = static_cast<U0>(LOAD_ARG0());
  U1 rhs = static_cast<U1>(LOAD_ARG1());
  STORE_RESULT((A0)(lhs * rhs));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(UMUL, UMUL, I8, I8, I8);
REGISTER_INT_CALLBACK(UMUL, UMUL, I16, I16, I16);
REGISTER_INT_CALLBACK(UMUL, UMUL, I32, I32, I32);
REGISTER_INT_CALLBACK(UMUL, UMUL, I64, I64, I64);

INT_CALLBACK(DIV) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs / rhs);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(DIV, DIV, I8, I8, I8);
REGISTER_INT_CALLBACK(DIV, DIV, I16, I16, I16);
REGISTER_INT_CALLBACK(DIV, DIV, I32, I32, I32);
REGISTER_INT_CALLBACK(DIV, DIV, I64, I64, I64);
REGISTER_INT_CALLBACK(DIV, DIV, F32, F32, F32);
REGISTER_INT_CALLBACK(DIV, DIV, F64, F64, F64);

INT_CALLBACK(NEG) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(-v);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(NEG, NEG, I8, I8, V);
REGISTER_INT_CALLBACK(NEG, NEG, I16, I16, V);
REGISTER_INT_CALLBACK(NEG, NEG, I32, I32, V);
REGISTER_INT_CALLBACK(NEG, NEG, I64, I64, V);
REGISTER_INT_CALLBACK(NEG, NEG, F32, F32, V);
REGISTER_INT_CALLBACK(NEG, NEG, F64, F64, V);

INT_CALLBACK(SQRTF) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(sqrtf(v));
  return NEXT_INSTR;
}
INT_CALLBACK(SQRT) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(sqrt(v));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SQRT, SQRTF, F32, F32, V);
REGISTER_INT_CALLBACK(SQRT, SQRT, F64, F64, V);

INT_CALLBACK(ABSF) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(fabs(v));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(ABS, ABSF, F32, F32, V);
REGISTER_INT_CALLBACK(ABS, ABSF, F64, F64, V);

INT_CALLBACK(SINF) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(sinf(v));
  return NEXT_INSTR;
}
INT_CALLBACK(SIN) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(sin(v));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SIN, SINF, F32, F32, V);
REGISTER_INT_CALLBACK(SIN, SIN, F64, F64, V);

INT_CALLBACK(COSF) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(cosf(v));
  return NEXT_INSTR;
}
INT_CALLBACK(COS) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(cos(v));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(COS, COSF, F32, F32, V);
REGISTER_INT_CALLBACK(COS, COS, F64, F64, V);

INT_CALLBACK(AND) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs & rhs);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(AND, AND, I8, I8, I8);
REGISTER_INT_CALLBACK(AND, AND, I16, I16, I16);
REGISTER_INT_CALLBACK(AND, AND, I32, I32, I32);
REGISTER_INT_CALLBACK(AND, AND, I64, I64, I64);

INT_CALLBACK(OR) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs | rhs);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(OR, OR, I8, I8, I8);
REGISTER_INT_CALLBACK(OR, OR, I16, I16, I16);
REGISTER_INT_CALLBACK(OR, OR, I32, I32, I32);
REGISTER_INT_CALLBACK(OR, OR, I64, I64, I64);

INT_CALLBACK(XOR) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs ^ rhs);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(XOR, XOR, I8, I8, I8);
REGISTER_INT_CALLBACK(XOR, XOR, I16, I16, I16);
REGISTER_INT_CALLBACK(XOR, XOR, I32, I32, I32);
REGISTER_INT_CALLBACK(XOR, XOR, I64, I64, I64);

INT_CALLBACK(NOT) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(~v);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(NOT, NOT, I8, I8, V);
REGISTER_INT_CALLBACK(NOT, NOT, I16, I16, V);
REGISTER_INT_CALLBACK(NOT, NOT, I32, I32, V);
REGISTER_INT_CALLBACK(NOT, NOT, I64, I64, V);

INT_CALLBACK(SHL) {
  A0 v = LOAD_ARG0();
  A1 n = LOAD_ARG1();
  STORE_RESULT(v << n);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(SHL, SHL, I8, I8, I32);
REGISTER_INT_CALLBACK(SHL, SHL, I16, I16, I32);
REGISTER_INT_CALLBACK(SHL, SHL, I32, I32, I32);
REGISTER_INT_CALLBACK(SHL, SHL, I64, I64, I32);

INT_CALLBACK(ASHR) {
  A0 v = LOAD_ARG0();
  A1 n = LOAD_ARG1();
  STORE_RESULT(v >> n);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(ASHR, ASHR, I8, I8, I32);
REGISTER_INT_CALLBACK(ASHR, ASHR, I16, I16, I32);
REGISTER_INT_CALLBACK(ASHR, ASHR, I32, I32, I32);
REGISTER_INT_CALLBACK(ASHR, ASHR, I64, I64, I32);

INT_CALLBACK(LSHR) {
  using U0 = typename std::make_unsigned<A0>::type;
  A0 v = LOAD_ARG0();
  A1 n = LOAD_ARG1();
  STORE_RESULT(static_cast<A0>(static_cast<U0>(v) >> n));
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(LSHR, LSHR, I8, I8, I32);
REGISTER_INT_CALLBACK(LSHR, LSHR, I16, I16, I32);
REGISTER_INT_CALLBACK(LSHR, LSHR, I32, I32, I32);
REGISTER_INT_CALLBACK(LSHR, LSHR, I64, I64, I32);

INT_CALLBACK(BRANCH) {
  using U0 = typename std::make_unsigned<A0>::type;
  U0 addr = static_cast<U0>(LOAD_ARG0());
  return addr;
}
REGISTER_INT_CALLBACK(BRANCH, BRANCH, V, I8, V);
REGISTER_INT_CALLBACK(BRANCH, BRANCH, V, I16, V);
REGISTER_INT_CALLBACK(BRANCH, BRANCH, V, I32, V);

INT_CALLBACK(BRANCH_COND) {
  using U1 = typename std::make_unsigned<A1>::type;
  A0 cond = LOAD_ARG0();
  if (cond) {
    U1 addr = static_cast<U1>(LOAD_ARG1());
    return addr;
  } else {
    U1 addr = static_cast<U1>(LOAD_ARG2());
    return addr;
  }
}
REGISTER_INT_CALLBACK(BRANCH_COND, BRANCH_COND, V, I8, I8);
REGISTER_INT_CALLBACK(BRANCH_COND, BRANCH_COND, V, I8, I16);
REGISTER_INT_CALLBACK(BRANCH_COND, BRANCH_COND, V, I8, I32);

INT_CALLBACK(CALL_EXTERNAL) {
  A0 addr = LOAD_ARG0();
  void (*func)(void *) = reinterpret_cast<void (*)(void *)>(addr);
  func(guest_ctx);
  return NEXT_INSTR;
}
REGISTER_INT_CALLBACK(CALL_EXTERNAL, CALL_EXTERNAL, V, I64, V);
