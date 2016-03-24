#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include <unordered_map>
#include "core/arena.h"
#include "core/assert.h"
#include "core/intrusive_list.h"

namespace re {
namespace jit {
namespace ir {

enum Op {
#define IR_OP(name) OP_##name,
#include "jit/ir/ir_ops.inc"
#undef IR_OP
  NUM_OPS
};

extern const char *Opnames[NUM_OPS];

//
// values
//
enum ValueType {
  VALUE_V,
  VALUE_I8,
  VALUE_I16,
  VALUE_I32,
  VALUE_I64,
  VALUE_F32,
  VALUE_F64,
  VALUE_NUM,
};

enum {
  VALUE_I8_MASK = 1 << VALUE_I8,
  VALUE_I16_MASK = 1 << VALUE_I16,
  VALUE_I32_MASK = 1 << VALUE_I32,
  VALUE_I64_MASK = 1 << VALUE_I64,
  VALUE_F32_MASK = 1 << VALUE_F32,
  VALUE_F64_MASK = 1 << VALUE_F64,
  VALUE_INT_MASK =
      VALUE_I8_MASK | VALUE_I16_MASK | VALUE_I32_MASK | VALUE_I64_MASK,
  VALUE_FLOAT_MASK = VALUE_F32_MASK | VALUE_F64_MASK,
  VALUE_ALL_MASK = VALUE_INT_MASK | VALUE_FLOAT_MASK,
};

enum {
  NO_REGISTER = -1,
};

class Instr;
class Use;

static inline bool IsFloatType(ValueType type) {
  return type == VALUE_F32 || type == VALUE_F64;
}

static inline bool IsIntType(ValueType type) { return !IsFloatType(type); }

static inline int SizeForType(ValueType type) {
  switch (type) {
    case VALUE_I8:
      return 1;
    case VALUE_I16:
      return 2;
    case VALUE_I32:
      return 4;
    case VALUE_I64:
      return 8;
    case VALUE_F32:
      return 4;
    case VALUE_F64:
      return 8;
    default:
      LOG_FATAL("Unexpected value type");
      break;
  }
}

class Value {
 public:
  Value(ValueType ty);
  Value(int8_t v);
  Value(int16_t v);
  Value(int32_t v);
  Value(int64_t v);
  Value(float v);
  Value(double v);

  ValueType type() const { return type_; }

  bool constant() const { return constant_; }

  // defined at the end of the file, Instr is only forward declared at this
  // point, it can't be static_cast to
  const Instr *def() const;
  Instr *def();

  int8_t i8() const {
    DCHECK(constant_ && type_ == VALUE_I8);
    return i8_;
  }
  int8_t i8() { return static_cast<const Value *>(this)->i8(); }
  int16_t i16() const {
    DCHECK(constant_ && type_ == VALUE_I16);
    return i16_;
  }
  int16_t i16() { return static_cast<const Value *>(this)->i16(); }
  int32_t i32() const {
    DCHECK(constant_ && type_ == VALUE_I32);
    return i32_;
  }
  int32_t i32() { return static_cast<const Value *>(this)->i32(); }
  int64_t i64() const {
    DCHECK(constant_ && type_ == VALUE_I64);
    return i64_;
  }
  int64_t i64() { return static_cast<const Value *>(this)->i64(); }
  float f32() const {
    DCHECK(constant_ && type_ == VALUE_F32);
    return f32_;
  }
  float f32() { return static_cast<const Value *>(this)->f32(); }
  double f64() const {
    DCHECK(constant_ && type_ == VALUE_F64);
    return f64_;
  }
  double f64() { return static_cast<const Value *>(this)->f64(); }

  const IntrusiveList<Use> &uses() const { return refs_; }
  IntrusiveList<Use> &uses() { return refs_; }

  int reg() const { return reg_; }
  void set_reg(int reg) { reg_ = reg; }

  intptr_t tag() const { return tag_; }
  void set_tag(intptr_t tag) { tag_ = tag; }

  uint64_t GetZExtValue() const;

  void AddRef(Use *ref);
  void RemoveRef(Use *ref);
  void ReplaceRefsWith(Value *other);

 private:
  const ValueType type_;
  const bool constant_;
  const union {
    int8_t i8_;
    int16_t i16_;
    int32_t i32_;
    int64_t i64_;
    float f32_;
    double f64_;
  };
  IntrusiveList<Use> refs_;
  // initializing here so each constructor variation doesn't have to
  int reg_{NO_REGISTER};
  intptr_t tag_{0};
};

// Use is a layer of indirection between an instruction and a values it uses.
// Values maintain a list of all of their uses, making it possible to replace
// all uses of a value with a new value during optimizations
class Use : public IntrusiveListNode<Use> {
 public:
  Use(Instr *instr);
  ~Use();

  const Instr *instr() const { return instr_; }
  Instr *instr() { return instr_; }

  const Value *value() const { return value_; }
  Value *value() { return value_; }
  void set_value(Value *v) {
    if (value_) {
      value_->RemoveRef(this);
    }
    value_ = v;
    value_->AddRef(this);
  }

 private:
  Instr *instr_;
  Value *value_;
};

// Templated structs to aid the interpreter / constant propagation handlers
template <int T>
struct ValueInfo;

template <>
struct ValueInfo<VALUE_V> {
  typedef void signed_type;
  constexpr static void (Value::*fn)() = nullptr;
};
template <>
struct ValueInfo<VALUE_I8> {
  typedef int8_t signed_type;
  typedef uint8_t unsigned_type;
  constexpr static int8_t (Value::*fn)() = &Value::i8;
};
template <>
struct ValueInfo<VALUE_I16> {
  typedef int16_t signed_type;
  typedef uint16_t unsigned_type;
  constexpr static int16_t (Value::*fn)() = &Value::i16;
};
template <>
struct ValueInfo<VALUE_I32> {
  typedef int32_t signed_type;
  typedef uint32_t unsigned_type;
  constexpr static int32_t (Value::*fn)() = &Value::i32;
};
template <>
struct ValueInfo<VALUE_I64> {
  typedef int64_t signed_type;
  typedef uint64_t unsigned_type;
  constexpr static int64_t (Value::*fn)() = &Value::i64;
};
template <>
struct ValueInfo<VALUE_F32> {
  typedef float signed_type;
  constexpr static float (Value::*fn)() = &Value::f32;
};
template <>
struct ValueInfo<VALUE_F64> {
  typedef double signed_type;
  constexpr static double (Value::*fn)() = &Value::f64;
};

// Locals are allocated for values that need to be spilled to the stack during
// register allocation.
class Local : public IntrusiveListNode<Local> {
 public:
  Local(ValueType ty, Value *offset);

  ValueType type() const { return type_; }
  Value *offset() const { return offset_; }

 private:
  ValueType type_;
  Value *offset_;
};

//
// instructions
//
class Instr : public Value, public IntrusiveListNode<Instr> {
 public:
  Instr(Op op, ValueType result_type);
  ~Instr();

  Op op() const { return op_; }

  const Value *arg0() const { return arg(0); }
  Value *arg0() { return arg(0); }
  void set_arg0(Value *v) { set_arg(0, v); }

  const Value *arg1() const { return arg(1); }
  Value *arg1() { return arg(1); }
  void set_arg1(Value *v) { set_arg(1, v); }

  const Value *arg2() const { return arg(2); }
  Value *arg2() { return arg(2); }
  void set_arg2(Value *v) { set_arg(2, v); }

  const Value *arg(int i) const {
    CHECK_LT(i, 3);
    return uses_[i].value();
  }
  Value *arg(int i) {
    CHECK_LT(i, 3);
    return uses_[i].value();
  }
  void set_arg(int i, Value *v) {
    CHECK_LT(i, 3);
    uses_[i].set_value(v);
  }

  intptr_t tag() const { return tag_; }
  void set_tag(intptr_t tag) { tag_ = tag; }

 private:
  Op op_;
  Use uses_[3];
  intptr_t tag_;
};

//
// IRBuilder
//
enum CmpType {
  CMP_EQ,
  CMP_NE,
  CMP_SGE,
  CMP_SGT,
  CMP_UGE,
  CMP_UGT,
  CMP_SLE,
  CMP_SLT,
  CMP_ULE,
  CMP_ULT
};

typedef void (*ExternalFn)(void *);

struct InsertPoint {
  Instr *instr;
};

class IRBuilder {
  friend class IRReader;

 public:
  IRBuilder();

  const IntrusiveList<Instr> &instrs() const { return instrs_; }
  IntrusiveList<Instr> &instrs() { return instrs_; }

  int locals_size() const { return locals_size_; }

  void Dump() const;

  InsertPoint GetInsertPoint();
  void SetInsertPoint(const InsertPoint &point);

  void RemoveInstr(Instr *instr);

  // direct access to host memory
  Instr *LoadHost(Value *addr, ValueType type);
  void StoreHost(Value *addr, Value *v);

  // guest memory operations
  Instr *LoadGuest(Value *addr, ValueType type);
  void StoreGuest(Value *addr, Value *v);

  // context operations
  Instr *LoadContext(size_t offset, ValueType type);
  void StoreContext(size_t offset, Value *v);

  // local operations
  Instr *LoadLocal(Local *local);
  void StoreLocal(Local *local, Value *v);

  // cast / conversion operations
  Instr *FToI(Value *v, ValueType dest_type);
  Instr *IToF(Value *v, ValueType dest_type);
  Instr *SExt(Value *v, ValueType dest_type);
  Instr *ZExt(Value *v, ValueType dest_type);
  Instr *Trunc(Value *v, ValueType dest_type);
  Instr *FExt(Value *v, ValueType dest_type);
  Instr *FTrunc(Value *v, ValueType dest_type);

  // conditionals
  Instr *Select(Value *cond, Value *t, Value *f);
  Instr *CmpEQ(Value *a, Value *b);
  Instr *CmpNE(Value *a, Value *b);
  Instr *CmpSGE(Value *a, Value *b);
  Instr *CmpSGT(Value *a, Value *b);
  Instr *CmpUGE(Value *a, Value *b);
  Instr *CmpUGT(Value *a, Value *b);
  Instr *CmpSLE(Value *a, Value *b);
  Instr *CmpSLT(Value *a, Value *b);
  Instr *CmpULE(Value *a, Value *b);
  Instr *CmpULT(Value *a, Value *b);
  Instr *FCmpEQ(Value *a, Value *b);
  Instr *FCmpNE(Value *a, Value *b);
  Instr *FCmpGE(Value *a, Value *b);
  Instr *FCmpGT(Value *a, Value *b);
  Instr *FCmpLE(Value *a, Value *b);
  Instr *FCmpLT(Value *a, Value *b);

  // math operators
  Instr *Add(Value *a, Value *b);
  Instr *Sub(Value *a, Value *b);
  Instr *SMul(Value *a, Value *b);
  Instr *UMul(Value *a, Value *b);
  Instr *Div(Value *a, Value *b);
  Instr *Neg(Value *a);
  Instr *Abs(Value *a);
  Instr *FAdd(Value *a, Value *b);
  Instr *FSub(Value *a, Value *b);
  Instr *FMul(Value *a, Value *b);
  Instr *FDiv(Value *a, Value *b);
  Instr *FNeg(Value *a);
  Instr *FAbs(Value *a);
  Instr *Sqrt(Value *a);

  // bitwise operations
  Instr *And(Value *a, Value *b);
  Instr *Or(Value *a, Value *b);
  Instr *Xor(Value *a, Value *b);
  Instr *Not(Value *a);
  Instr *Shl(Value *a, Value *n);
  Instr *Shl(Value *a, int n);
  Instr *AShr(Value *a, Value *n);
  Instr *AShr(Value *a, int n);
  Instr *LShr(Value *a, Value *n);
  Instr *LShr(Value *a, int n);
  Instr *AShd(Value *a, Value *n);
  Instr *LShd(Value *a, Value *n);

  // calls
  void CallExternal1(Value *addr);
  void CallExternal2(Value *addr, Value *arg0);

  // values
  Value *AllocConstant(uint8_t c);
  Value *AllocConstant(uint16_t c);
  Value *AllocConstant(uint32_t c);
  Value *AllocConstant(uint64_t c);
  Value *AllocConstant(int8_t c);
  Value *AllocConstant(int16_t c);
  Value *AllocConstant(int32_t c);
  Value *AllocConstant(int64_t c);
  Value *AllocConstant(float c);
  Value *AllocConstant(double c);
  Local *AllocLocal(ValueType type);

 protected:
  Instr *AllocInstr(Op op, ValueType result_type);
  Instr *AppendInstr(Op op);
  Instr *AppendInstr(Op op, ValueType result_type);

  Instr *Cmp(Value *a, Value *b, CmpType type);
  Instr *FCmp(Value *a, Value *b, CmpType type);

  Arena arena_;
  IntrusiveList<Instr> instrs_;
  Instr *current_instr_;
  IntrusiveList<Local> locals_;
  int locals_size_;
};

inline const Instr *Value::def() const {
  CHECK(!constant_);
  return static_cast<const Instr *>(this);
}

inline Instr *Value::def() {
  CHECK(!constant_);
  return static_cast<Instr *>(this);
}
}
}
}

#endif
