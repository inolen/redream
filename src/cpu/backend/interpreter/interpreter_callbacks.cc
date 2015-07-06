#include <iostream>
#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/backend/interpreter/interpreter_callbacks.h"

using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::interpreter;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

//
// callback lookup table generation
//
// callback functions are pregenerated for each instruction, for each possible
// permutation of arguments. each argument has a type, as well as if it's an
// immediate encoded in the instruction or not.
//
// this avoids several ext / truncation operations at runtime as well as
// branches to deal with either pulling the argument from a register or decoding
// it from the instruction itself.
//
// NOTE: OP_SELECT and OP_BRANCH_COND are the only instructions using arg2, and
// arg2's type always matches arg1's. because of this, and in order to save
// some memory, arg2 isn't considering when generating the lookup table.
//
enum { SIG_V, SIG_I8, SIG_I16, SIG_I32, SIG_I64, SIG_F32, SIG_F64, SIG_NUM };
enum { IMM_ARG0 = 1, IMM_ARG1 = 2, IMM_ARG2 = 4, IMM_MAX = 8 };

template <int T>
struct SigType;
template <>
struct SigType<0> {
  typedef void type;
};
template <>
struct SigType<SIG_I8> {
  typedef int8_t type;
};
template <>
struct SigType<SIG_I16> {
  typedef int16_t type;
};
template <>
struct SigType<SIG_I32> {
  typedef int32_t type;
};
template <>
struct SigType<SIG_I64> {
  typedef int64_t type;
};
template <>
struct SigType<SIG_F32> {
  typedef float type;
};
template <>
struct SigType<SIG_F64> {
  typedef double type;
};

#define MAX_CALLBACKS_PER_OP (SIG_NUM * SIG_NUM * SIG_NUM * IMM_MAX)
#define MAX_CALLBACKS (MAX_CALLBACKS_PER_OP * NUM_OPCODES)

static IntFn int_cbs[MAX_CALLBACKS];

inline int TRANSLATE_TYPE(const Value *v) {
  if (!v) {
    return SIG_V;
  } else if (v->type() == VALUE_BLOCK) {
    return SIG_I32;
  } else {
    // ValueTy dosn't have a null type, but one is needed for argument
    // types, offset by 1 leaving 0 for SIG_V
    return v->type() + 1;
  }
}

// each argument's data type, as well as if it's encoded as an immediate or
// not is used when indexing into the callback table
inline int CALLBACK_IDX(Opcode op, int r, int a0, int a1, int imm_mask) {
  return MAX_CALLBACKS_PER_OP * op +
         (((r * SIG_NUM * SIG_NUM) + (a0 * SIG_NUM) + a1) * IMM_MAX) + imm_mask;
}

IntFn dreavm::cpu::backend::interpreter::GetCallback(const Instr *instr) {
  int imm_mask = 0;
  if (instr->arg0() && instr->arg0()->constant()) {
    imm_mask |= IMM_ARG0;
  }
  if (instr->arg1() && instr->arg1()->constant()) {
    imm_mask |= IMM_ARG1;
  }
  if (instr->arg2() && instr->arg2()->constant()) {
    imm_mask |= IMM_ARG2;
  }
  IntFn fn = int_cbs[CALLBACK_IDX(instr->op(), TRANSLATE_TYPE(instr->result()),
                                  TRANSLATE_TYPE(instr->arg0()),
                                  TRANSLATE_TYPE(instr->arg1()), imm_mask)];
  CHECK_NOTNULL(fn);
  return fn;
}

//
// callback helpers
//
template <typename T>
static inline T GetRegister(const IntReg &r);
template <>
inline int8_t GetRegister(const IntReg &r) {
  return r.i8;
}
template <>
inline int16_t GetRegister(const IntReg &r) {
  return r.i16;
}
template <>
inline int32_t GetRegister(const IntReg &r) {
  return r.i32;
}
template <>
inline int64_t GetRegister(const IntReg &r) {
  return r.i64;
}
template <>
inline float GetRegister(const IntReg &r) {
  return r.f32;
}
template <>
inline double GetRegister(const IntReg &r) {
  return r.f64;
}

template <typename T>
static inline void SetRegister(IntReg &r, T v);
template <>
inline void SetRegister(IntReg &r, int8_t v) {
  r.i8 = v;
}
template <>
inline void SetRegister(IntReg &r, int16_t v) {
  r.i16 = v;
}
template <>
inline void SetRegister(IntReg &r, int32_t v) {
  r.i32 = v;
}
template <>
inline void SetRegister(IntReg &r, int64_t v) {
  r.i64 = v;
}
template <>
inline void SetRegister(IntReg &r, float v) {
  r.f32 = v;
}
template <>
inline void SetRegister(IntReg &r, double v) {
  r.f64 = v;
}

// LoadArg is specialized not only to load the correct argument based on its
// data type, but also to choose to either decode it from the instruction or
// load it from a register
template <typename T, int IDX, int IMM_MASK, class ENABLE = void>
struct helper {
  static inline T LoadArg(IntReg *r, IntInstr *i);
};
template <typename T, int ARG, int IMM_MASK>
struct helper<T, ARG, IMM_MASK,
              typename std::enable_if<(IMM_MASK & (1 << ARG)) == 0>::type> {
  static inline T LoadArg(IntReg *r, IntInstr *i) {
    return GetRegister<T>(r[i->arg[ARG].i32]);
  }
};
template <typename T, int ARG, int IMM_MASK>
struct helper<T, ARG, IMM_MASK,
              typename std::enable_if<(IMM_MASK & (1 << ARG)) != 0>::type> {
  static inline T LoadArg(IntReg *r, IntInstr *i) {
    return GetRegister<T>(i->arg[ARG]);
  }
};

#define CALLBACK(name)                                                 \
  template <typename R = void, typename A0 = void, typename A1 = void, \
            int IMM_MASK = 0>                                          \
  static uint32_t name(Memory *memory, void *guest_ctx, IntReg *r,     \
                       IntInstr *i, uint32_t idx)
#define LOAD_ARG0() helper<A0, 0, IMM_MASK>::LoadArg(r, i)
#define LOAD_ARG1() helper<A1, 1, IMM_MASK>::LoadArg(r, i)
#define LOAD_ARG2() helper<A1, 2, IMM_MASK>::LoadArg(r, i)
#define STORE_RESULT(v) SetRegister<R>(r[i->result], v);
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

CALLBACK(LOAD_RAW_I8) {
  uint64_t addr = LOAD_ARG0();
  STORE_RESULT(*reinterpret_cast<uint8_t *>(addr));
  return NEXT_INSTR;
}

CALLBACK(LOAD_RAW_I16) {
  uint64_t addr = LOAD_ARG0();
  STORE_RESULT(*reinterpret_cast<uint16_t *>(addr));
  return NEXT_INSTR;
}

CALLBACK(LOAD_RAW_I32) {
  uint64_t addr = LOAD_ARG0();
  STORE_RESULT(*reinterpret_cast<uint32_t *>(addr));
  return NEXT_INSTR;
}

CALLBACK(LOAD_RAW_I64) {
  uint64_t addr = LOAD_ARG0();
  STORE_RESULT(*reinterpret_cast<uint64_t *>(addr));
  return NEXT_INSTR;
}

CALLBACK(LOAD_RAW_F32) {
  uint64_t addr = LOAD_ARG0();
  STORE_RESULT(*reinterpret_cast<float *>(addr));
  return NEXT_INSTR;
}

CALLBACK(LOAD_RAW_F64) {
  uint64_t addr = LOAD_ARG0();
  STORE_RESULT(*reinterpret_cast<double *>(addr));
  return NEXT_INSTR;
}

CALLBACK(LOAD_DYN_I8) {
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG0());
  uint32_t offset = LOAD_ARG1();
  R v = page->force32 ? page->r32(page->ctx, offset)
                      : page->r8(page->ctx, offset);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(LOAD_DYN_I16) {
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG0());
  uint32_t offset = LOAD_ARG1();
  R v = page->force32 ? page->r32(page->ctx, offset)
                      : page->r16(page->ctx, offset);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(LOAD_DYN_I32) {
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG0());
  uint32_t offset = LOAD_ARG1();
  R v = page->r32(page->ctx, offset);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(LOAD_DYN_I64) {
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG0());
  uint32_t offset = LOAD_ARG1();
  R v = page->r64(page->ctx, offset);
  STORE_RESULT(v);
  return NEXT_INSTR;
}

CALLBACK(LOAD_DYN_F32) {
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG0());
  uint32_t offset = LOAD_ARG1();
  uint32_t v = page->r32(page->ctx, offset);
  STORE_RESULT(*reinterpret_cast<float *>(v));
  return NEXT_INSTR;
}

CALLBACK(LOAD_DYN_F64) {
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG0());
  uint32_t offset = LOAD_ARG1();
  uint64_t v = page->r64(page->ctx, offset);
  STORE_RESULT(*reinterpret_cast<double *>(v));
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

CALLBACK(STORE_RAW_I8) {
  A0 v = LOAD_ARG0();
  uint64_t addr = LOAD_ARG1();
  *reinterpret_cast<uint8_t *>(addr) = v;
  return NEXT_INSTR;
}

CALLBACK(STORE_RAW_I16) {
  A0 v = LOAD_ARG0();
  uint64_t addr = LOAD_ARG1();
  *reinterpret_cast<uint16_t *>(addr) = v;
  return NEXT_INSTR;
}

CALLBACK(STORE_RAW_I32) {
  A0 v = LOAD_ARG0();
  uint64_t addr = LOAD_ARG1();
  *reinterpret_cast<uint32_t *>(addr) = v;
  return NEXT_INSTR;
}

CALLBACK(STORE_RAW_I64) {
  A0 v = LOAD_ARG0();
  uint64_t addr = LOAD_ARG1();
  *reinterpret_cast<uint64_t *>(addr) = v;
  return NEXT_INSTR;
}

CALLBACK(STORE_RAW_F32) {
  A0 v = LOAD_ARG0();
  uint64_t addr = LOAD_ARG1();
  *reinterpret_cast<float *>(addr) = v;
  return NEXT_INSTR;
}

CALLBACK(STORE_RAW_F64) {
  A0 v = LOAD_ARG0();
  uint64_t addr = LOAD_ARG1();
  *reinterpret_cast<double *>(addr) = v;
  return NEXT_INSTR;
}

CALLBACK(STORE_DYN_I8) {
  A0 v = LOAD_ARG0();
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG1());
  uint32_t offset = LOAD_ARG2();
  if (page->force32) {
    page->w32(page->ctx, offset, v);
  } else {
    page->w8(page->ctx, offset, v);
  }
  return NEXT_INSTR;
}

CALLBACK(STORE_DYN_I16) {
  A0 v = LOAD_ARG0();
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG1());
  uint32_t offset = LOAD_ARG2();
  if (page->force32) {
    page->w32(page->ctx, offset, v);
  } else {
    page->w16(page->ctx, offset, v);
  }
  return NEXT_INSTR;
}

CALLBACK(STORE_DYN_I32) {
  A0 v = LOAD_ARG0();
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG1());
  uint32_t offset = LOAD_ARG2();
  page->w32(page->ctx, offset, v);
  return NEXT_INSTR;
}

CALLBACK(STORE_DYN_I64) {
  A0 v = LOAD_ARG0();
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG1());
  uint32_t offset = LOAD_ARG2();
  page->w32(page->ctx, offset, v);
  return NEXT_INSTR;
}

CALLBACK(STORE_DYN_F32) {
  A0 v = LOAD_ARG0();
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG1());
  uint32_t offset = LOAD_ARG2();
  page->w32(page->ctx, offset, *reinterpret_cast<uint32_t *>(&v));
  return NEXT_INSTR;
}

CALLBACK(STORE_DYN_F64) {
  A0 v = LOAD_ARG0();
  MemoryBank *page = reinterpret_cast<MemoryBank *>(LOAD_ARG1());
  uint32_t offset = LOAD_ARG2();
  page->w64(page->ctx, offset, *reinterpret_cast<uint64_t *>(&v));
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

// This is terribly slow to compile (takes about ~1 minute on my MBP), but it
// does boost interpreter speed by 5-10% over having immediate conditionals
// inside of each LoadArg call. Ideally, once the x64 backend is functional
// I believe the build will just not include the interpreter by default.
static void InitCallbacks() {
#define INT_CALLBACK_C(op, func, r, a0, a1, c)                \
  int_cbs[CALLBACK_IDX(op, SIG_##r, SIG_##a0, SIG_##a1, c)] = \
      &func<SigType<SIG_##r>::type, SigType<SIG_##a0>::type,  \
            SigType<SIG_##a1>::type, c>;
#define INT_CALLBACK(op, func, r, a0, a1) \
  INT_CALLBACK_C(op, func, r, a0, a1, 0)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 1)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 2)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 3)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 4)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 5)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 6)  \
  INT_CALLBACK_C(op, func, r, a0, a1, 7)  // IMM_MAX
#include "cpu/backend/interpreter/interpreter_callbacks.inc"
#undef INT_CALLBACK
}

static struct _cb_init {
  _cb_init() { InitCallbacks(); }
} cb_init;
