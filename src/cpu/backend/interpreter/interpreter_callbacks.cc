#include <iostream>
#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/backend/interpreter/interpreter_callbacks.h"

using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::interpreter;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

//
// helpers for loading / storing arguments
//
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

//
// ACC_REG
//
// argument is located in a virtual register, arg->i32 specifies the register
//
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

//
// ACC_LCL
//
// argument is located on the stack, arg->i32 specifies the stack offset
//
template <typename T, int ARG, IntAccessMask ACCESS_MASK>
struct helper<
    T, ARG, ACCESS_MASK,
    typename std::enable_if<GetArgAccess(ACCESS_MASK, ARG) == ACC_LCL>::type> {
  static inline T LoadArg(const IntInstr *i, const IntValue *r,
                          const uint8_t *l) {
    return GetLocal<T>(l, i->arg[ARG].i32);
  }

  static inline void StoreArg(const IntInstr *i, IntValue *r, uint8_t *l, T v) {
    SetLocal<T>(l, i->arg[ARG].i32, v);
  }
};

//
// ACC_IMM
//
// argument is encoded directly on the instruction
//
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

#define CALLBACK(name)                                                  \
  template <typename R = void, typename A0 = void, typename A1 = void,  \
            int ACCESS_MASK = 0>                                        \
  static uint32_t name(const IntInstr *i, uint32_t idx, Memory *memory, \
                       IntValue *r, uint8_t *locals, void *guest_ctx)
#define LOAD_ARG0() helper<A0, 0, ACCESS_MASK>::LoadArg(i, r, locals)
#define LOAD_ARG1() helper<A1, 1, ACCESS_MASK>::LoadArg(i, r, locals)
#define LOAD_ARG2() helper<A1, 2, ACCESS_MASK>::LoadArg(i, r, locals)
#define STORE_RESULT(v) helper<R, 3, ACCESS_MASK>::StoreArg(i, r, locals, v)
#define NEXT_INSTR (idx + 1)

//
// interpreter callbacks
//
CALLBACK(PRINTF) {
  std::cout << LOAD_ARG0() << std::endl;
  return NEXT_INSTR;
}

CALLBACK(LOAD_CONTEXT) {
  A0 offset = LOAD_ARG0();
  R v = *reinterpret_cast<R *>(reinterpret_cast<uint8_t *>(guest_ctx) + offset);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(STORE_CONTEXT) {
  A0 offset = LOAD_ARG0();
  A1 v = LOAD_ARG1();
  *reinterpret_cast<A1 *>(reinterpret_cast<uint8_t *>(guest_ctx) + offset) = v;
  return NEXT_INSTR;
}

CALLBACK(LOAD_I8) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->R8(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(LOAD_I16) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->R16(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(LOAD_I32) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->R32(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(LOAD_I64) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->R64(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(LOAD_F32) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->RF32(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(LOAD_F64) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  R v = memory->RF64(addr);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(STORE_I8) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W8(addr, v);
  return NEXT_INSTR;
}

CALLBACK(STORE_I16) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W16(addr, v);
  return NEXT_INSTR;
}

CALLBACK(STORE_I32) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W32(addr, v);
  return NEXT_INSTR;
}

CALLBACK(STORE_I64) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->W64(addr, v);
  return NEXT_INSTR;
}

CALLBACK(STORE_F32) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->WF32(addr, v);
  return NEXT_INSTR;
}

CALLBACK(STORE_F64) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  A1 v = LOAD_ARG1();
  memory->WF64(addr, v);
  return NEXT_INSTR;
}

CALLBACK(CAST) {
  A0 v = LOAD_ARG0();
  STORE_RESULT((R)v);
  return NEXT_INSTR;
}

CALLBACK(SEXT) {
  A0 v = LOAD_ARG0();
  STORE_RESULT((R)v);
  return NEXT_INSTR;
}

CALLBACK(ZEXT) {
  using U0 = typename std::make_unsigned<A0>::type;
  A0 v = LOAD_ARG0();
  STORE_RESULT((R)(U0)v);
  return NEXT_INSTR;
}

CALLBACK(TRUNCATE) {
  using U0 = typename std::make_unsigned<A0>::type;
  A0 v = LOAD_ARG0();
  STORE_RESULT((R)(U0)v);
  return NEXT_INSTR;
}

CALLBACK(SELECT) {
  A0 cond = LOAD_ARG0();
  A1 t = LOAD_ARG1();
  A1 f = LOAD_ARG2();
  STORE_RESULT(cond ? t : f);
  return NEXT_INSTR;
}

CALLBACK(EQ) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs == rhs));
  return NEXT_INSTR;
}

CALLBACK(NE) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs != rhs));
  return NEXT_INSTR;
}

CALLBACK(SGE) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs >= rhs));
  return NEXT_INSTR;
}

CALLBACK(SGT) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs > rhs));
  return NEXT_INSTR;
}

CALLBACK(UGE) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = (U0)LOAD_ARG0();
  U1 rhs = (U1)LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs >= rhs));
  return NEXT_INSTR;
}

CALLBACK(UGT) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = (U0)LOAD_ARG0();
  U1 rhs = (U1)LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs > rhs));
  return NEXT_INSTR;
}

CALLBACK(SLE) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs <= rhs));
  return NEXT_INSTR;
}

CALLBACK(SLT) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs < rhs));
  return NEXT_INSTR;
}

CALLBACK(ULE) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = (U0)LOAD_ARG0();
  U1 rhs = (U1)LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs <= rhs));
  return NEXT_INSTR;
}

CALLBACK(ULT) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = (U0)LOAD_ARG0();
  U1 rhs = (U1)LOAD_ARG1();
  STORE_RESULT((int8_t)(lhs < rhs));
  return NEXT_INSTR;
}

CALLBACK(ADD) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs + rhs);
  return NEXT_INSTR;
}

CALLBACK(SUB) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs - rhs);
  return NEXT_INSTR;
}

CALLBACK(SMUL) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs * rhs);
  return NEXT_INSTR;
}

CALLBACK(UMUL) {
  using U0 = typename std::make_unsigned<A0>::type;
  using U1 = typename std::make_unsigned<A1>::type;
  U0 lhs = (U0)LOAD_ARG0();
  U1 rhs = (U1)LOAD_ARG1();
  STORE_RESULT((A0)(lhs * rhs));
  return NEXT_INSTR;
}

CALLBACK(DIV) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs / rhs);
  return NEXT_INSTR;
}

CALLBACK(NEG) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(-v);
  return NEXT_INSTR;
}

CALLBACK(SQRTF) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(sqrtf(v));
  return NEXT_INSTR;
}

CALLBACK(SQRT) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(sqrt(v));
  return NEXT_INSTR;
}

CALLBACK(ABSF) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(fabs(v));
  return NEXT_INSTR;
}

CALLBACK(SINF) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(sinf(v));
  return NEXT_INSTR;
}

CALLBACK(SIN) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(sin(v));
  return NEXT_INSTR;
}

CALLBACK(COSF) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(cosf(v));
  return NEXT_INSTR;
}

CALLBACK(COS) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(cos(v));
  return NEXT_INSTR;
}

CALLBACK(AND) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs & rhs);
  return NEXT_INSTR;
}

CALLBACK(OR) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs | rhs);
  return NEXT_INSTR;
}

CALLBACK(XOR) {
  A0 lhs = LOAD_ARG0();
  A1 rhs = LOAD_ARG1();
  STORE_RESULT(lhs ^ rhs);
  return NEXT_INSTR;
}

CALLBACK(NOT) {
  A0 v = LOAD_ARG0();
  STORE_RESULT(~v);
  return NEXT_INSTR;
}

CALLBACK(SHL) {
  A0 v = LOAD_ARG0();
  A1 n = LOAD_ARG1();
  STORE_RESULT(v << n);
  return NEXT_INSTR;
}

CALLBACK(ASHR) {
  A0 v = LOAD_ARG0();
  A1 n = LOAD_ARG1();
  STORE_RESULT(v >> n);
  return NEXT_INSTR;
}

CALLBACK(LSHR) {
  using U0 = typename std::make_unsigned<A0>::type;
  A0 v = LOAD_ARG0();
  A1 n = LOAD_ARG1();
  STORE_RESULT((A0)((U0)v >> n));
  return NEXT_INSTR;
}

CALLBACK(BRANCH) {
  using U0 = typename std::make_unsigned<A0>::type;
  U0 addr = (U0)LOAD_ARG0();
  return addr;
}

CALLBACK(BRANCH_COND) {
  using U1 = typename std::make_unsigned<A1>::type;
  A0 cond = LOAD_ARG0();
  if (cond) {
    U1 addr = (U1)LOAD_ARG1();
    return addr;
  } else {
    U1 addr = (U1)LOAD_ARG2();
    return addr;
  }
}

CALLBACK(BRANCH_INDIRECT) {
  uint32_t addr = (uint32_t)LOAD_ARG0();
  return addr;
}

CALLBACK(CALL_EXTERNAL) {
  A0 addr = LOAD_ARG0();
  void (*func)(void *) = (void (*)(void *))(intptr_t)addr;
  func(guest_ctx);
  return NEXT_INSTR;
}

//
// callback lookup table generation
//
// callback functions are generated for each instruction, for each possible
// permutation of arguments. each argument has a data type, as well as its
// access type.
//
// NOTE: OP_SELECT and OP_BRANCH_COND are the only instructions using arg2, and
// arg2's type always matches arg1's. because of this, and in order to save
// some memory, arg2 isn't considering when generating the lookup table.
//
#define MAX_CALLBACKS_PER_OP \
  (VALUE_NUM * VALUE_NUM * VALUE_NUM * NUM_ACC_COMBINATIONS)
#define MAX_CALLBACKS (MAX_CALLBACKS_PER_OP * NUM_OPCODES)
#define CALLBACK_IDX(op, result_sig, arg0_sig, arg1_sig, access_mask)   \
  (MAX_CALLBACKS_PER_OP * op + (((result_sig * VALUE_NUM * VALUE_NUM) + \
                                 (arg0_sig * VALUE_NUM) + arg1_sig) *   \
                                NUM_ACC_COMBINATIONS) +                 \
   access_mask)

static IntFn int_cbs[MAX_CALLBACKS];

IntFn dreavm::cpu::backend::interpreter::GetCallback(
    Opcode op, const IntSig &sig, IntAccessMask access_mask) {
  IntFn fn =
      int_cbs[CALLBACK_IDX(op, GetArgSignature(sig, 3), GetArgSignature(sig, 0),
                           GetArgSignature(sig, 1), access_mask)];
  CHECK_NOTNULL(fn);
  return fn;
}

static void InitCallbacks() {
// Generate NUM_ACC_COMBINATIONS callbacks for each op, excluding access masks
// where (mask & 0x3), (mask >> 2) & 0x3, or (mask >> 4) & 0x3 are equal to 3,
// as they're invalid.
#define INT_CALLBACK_C(op, func, r, a0, a1, c)                       \
  int_cbs[CALLBACK_IDX(op, VALUE_##r, VALUE_##a0, VALUE_##a1, c)] =  \
      &func<ValueType<VALUE_##r>::type, ValueType<VALUE_##a0>::type, \
            ValueType<VALUE_##a1>::type, c>;
#define INT_CALLBACK(op, func, r, a0, a1)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 0)   \
  INT_CALLBACK_C(op, func, r, a0, a1, 1)   \
  INT_CALLBACK_C(op, func, r, a0, a1, 2)   \
  INT_CALLBACK_C(op, func, r, a0, a1, 4)   \
  INT_CALLBACK_C(op, func, r, a0, a1, 5)   \
  INT_CALLBACK_C(op, func, r, a0, a1, 6)   \
  INT_CALLBACK_C(op, func, r, a0, a1, 8)   \
  INT_CALLBACK_C(op, func, r, a0, a1, 9)   \
  INT_CALLBACK_C(op, func, r, a0, a1, 10)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 16)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 17)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 18)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 20)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 21)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 22)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 24)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 25)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 26)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 32)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 33)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 34)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 36)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 37)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 38)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 40)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 41)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 42)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 64)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 65)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 66)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 68)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 69)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 70)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 72)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 73)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 74)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 80)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 81)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 82)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 84)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 85)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 86)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 88)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 89)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 90)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 96)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 97)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 98)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 100) \
  INT_CALLBACK_C(op, func, r, a0, a1, 101) \
  INT_CALLBACK_C(op, func, r, a0, a1, 102) \
  INT_CALLBACK_C(op, func, r, a0, a1, 104) \
  INT_CALLBACK_C(op, func, r, a0, a1, 105) \
  INT_CALLBACK_C(op, func, r, a0, a1, 106)
#include "cpu/backend/interpreter/interpreter_callbacks.inc"
#undef INT_CALLBACK
}

static struct _cb_init {
  _cb_init() { InitCallbacks(); }
} cb_init;
