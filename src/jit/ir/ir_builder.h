#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include <unordered_map>
#include "core/core.h"
#include "core/intrusive_list.h"

namespace dreavm {
namespace jit {
namespace ir {

enum Opcode {
#define IR_OP(name) OP_##name,
#include "jit/ir/ir_ops.inc"
#undef IR_OP
  NUM_OPCODES
};

extern const char *Opnames[NUM_OPCODES];

//
// values
//
enum ValueTy {
  VALUE_I8 = 1,
  VALUE_I16,
  VALUE_I32,
  VALUE_I64,
  VALUE_F32,
  VALUE_F64
};
enum {
  // not used by IRBuilder directly, but useful when generating lookup tables
  VALUE_V = 0,
  VALUE_NUM = VALUE_F64 + 1
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
  VALUE_ALL_MASK = VALUE_I8_MASK | VALUE_I16_MASK | VALUE_I32_MASK |
                   VALUE_I64_MASK | VALUE_F32_MASK | VALUE_F64_MASK,
};
enum { NO_REGISTER = -1 };

template <int T>
struct ValueType;
template <>
struct ValueType<VALUE_V> {
  typedef void type;
};
template <>
struct ValueType<VALUE_I8> {
  typedef int8_t type;
};
template <>
struct ValueType<VALUE_I16> {
  typedef int16_t type;
};
template <>
struct ValueType<VALUE_I32> {
  typedef int32_t type;
};
template <>
struct ValueType<VALUE_I64> {
  typedef int64_t type;
};
template <>
struct ValueType<VALUE_F32> {
  typedef float type;
};
template <>
struct ValueType<VALUE_F64> {
  typedef double type;
};

class Block;
class Instr;
class ValueRef;

static inline bool IsFloatType(ValueTy type) {
  return type == VALUE_F32 || type == VALUE_F64;
}

static inline bool IsIntType(ValueTy type) { return !IsFloatType(type); }

static inline int SizeForType(ValueTy type) {
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
  }
  return 0;
}

class Value {
 public:
  Value(ValueTy ty);
  Value(int8_t v);
  Value(int16_t v);
  Value(int32_t v);
  Value(int64_t v);
  Value(float v);
  Value(double v);

  ValueTy type() const { return type_; }

  bool constant() const { return constant_; }

  template <typename T>
  T value() const;
  template <typename T>
  T value();

  const IntrusiveList<ValueRef> &refs() const { return refs_; }
  IntrusiveList<ValueRef> &refs() { return refs_; }

  int reg() const { return reg_; }
  void set_reg(int reg) { reg_ = reg; }

  intptr_t tag() const { return tag_; }
  void set_tag(intptr_t tag) { tag_ = tag; }

  uint64_t GetZExtValue() const;

  void AddRef(ValueRef *ref);
  void RemoveRef(ValueRef *ref);
  void ReplaceRefsWith(Value *other);

 private:
  const ValueTy type_;
  const bool constant_;
  const union {
    int8_t i8_;
    int16_t i16_;
    int32_t i32_;
    int64_t i64_;
    float f32_;
    double f64_;
  };
  IntrusiveList<ValueRef> refs_;
  // initializing here so each constructor variation doesn't have to
  int reg_{NO_REGISTER};
  intptr_t tag_{0};
};

template <>
inline int8_t Value::value() const {
  DCHECK(constant_ && type_ == VALUE_I8);
  return i8_;
}
template <>
inline int8_t Value::value() {
  DCHECK(constant_ && type_ == VALUE_I8);
  return i8_;
}
template <>
inline int16_t Value::value() const {
  DCHECK(constant_ && type_ == VALUE_I16);
  return i16_;
}
template <>
inline int16_t Value::value() {
  DCHECK(constant_ && type_ == VALUE_I16);
  return i16_;
}
template <>
inline int32_t Value::value() const {
  DCHECK(constant_ && type_ == VALUE_I32);
  return i32_;
}
template <>
inline int32_t Value::value() {
  DCHECK(constant_ && type_ == VALUE_I32);
  return i32_;
}
template <>
inline int64_t Value::value() const {
  DCHECK(constant_ && type_ == VALUE_I64);
  return i64_;
}
template <>
inline int64_t Value::value() {
  DCHECK(constant_ && type_ == VALUE_I64);
  return i64_;
}
template <>
inline float Value::value() const {
  DCHECK(constant_ && type_ == VALUE_F32);
  return f32_;
}
template <>
inline float Value::value() {
  DCHECK(constant_ && type_ == VALUE_F32);
  return f32_;
}
template <>
inline double Value::value() const {
  DCHECK(constant_ && type_ == VALUE_F64);
  return f64_;
}
template <>
inline double Value::value() {
  DCHECK(constant_ && type_ == VALUE_F64);
  return f64_;
}

// ValueRef is a layer of indirection between an instruction and a values it
// uses. Values maintain a list of all of their references, making it possible
// during optimization to replace all references to a value with a new value.
class ValueRef : public IntrusiveListNode<ValueRef> {
 public:
  ValueRef(Instr *instr);
  ~ValueRef();

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

// Locals are allocated for values that need to be spilled to the stack during
// register allocation. When allocated, a default offset of 0 is assigned,
// each backend is expected to update the offset to an appropriate value
// before emitting.
class Local : public IntrusiveListNode<Local> {
 public:
  Local(ValueTy ty, Value *offset);

  ValueTy type() const { return type_; }
  ValueTy type() { return type_; }

  Value *offset() const { return offset_; }
  Value *offset() { return offset_; }
  void set_offset(Value *offset) {
    offset_->ReplaceRefsWith(offset);
    offset_ = offset;
  }

 private:
  ValueTy type_;
  Value *offset_;
};

//
// instructions
//
enum InstrFlag { IF_NONE = 0x0, IF_INVALIDATE_CONTEXT = 0x1 };

class Instr : public IntrusiveListNode<Instr> {
  friend class Block;

 public:
  Instr(Opcode op, InstrFlag flags);
  ~Instr();

  const Block *block() const { return block_; }
  Block *block() { return block_; }

  Opcode op() const { return op_; }

  InstrFlag flags() const { return flags_; }

  const Value *arg0() const { return arg(0); }
  Value *arg0() { return arg(0); }
  void set_arg0(Value *v) { set_arg(0, v); }

  const Value *arg1() const { return arg(1); }
  Value *arg1() { return arg(1); }
  void set_arg1(Value *v) { set_arg(1, v); }

  const Value *arg2() const { return arg(2); }
  Value *arg2() { return arg(2); }
  void set_arg2(Value *v) { set_arg(2, v); }

  const Value *result() const { return arg(3); }
  Value *result() { return arg(3); }
  void set_result(Value *v) { set_arg(3, v); }

  const Value *arg(int i) const { return args_[i].value(); }
  Value *arg(int i) { return args_[i].value(); }
  void set_arg(int i, Value *v) { args_[i].set_value(v); }

  intptr_t tag() const { return tag_; }
  void set_tag(intptr_t tag) { tag_ = tag; }

 private:
  Block *set_block(Block *block) { return block_ = block; }

  Block *block_;
  Opcode op_;
  InstrFlag flags_;
  ValueRef args_[4];
  intptr_t tag_;
};

//
// blocks
//
class Block : public IntrusiveListNode<Block> {
  friend class IRBuilder;

 public:
  Block();
  ~Block();

  const IntrusiveList<Instr> &instrs() const { return instrs_; }
  IntrusiveList<Instr> &instrs() { return instrs_; }

  intptr_t tag() const { return tag_; }
  void set_tag(intptr_t tag) { tag_ = tag; }

  void InsertInstr(Instr *after, Instr *instr);
  void ReplaceInstr(Instr *replace, Instr *with);
  void RemoveInstr(Instr *instr);
  void UnlinkInstr(Instr *instr);

 private:
  IntrusiveList<Instr> instrs_;
  intptr_t tag_;
};

//
// IRBuilder
//
typedef void (*ExternalFn)(void *);

struct InsertPoint {
  Block *block;
  Instr *instr;
};

class IRBuilder {
 public:
  IRBuilder();

  const IntrusiveList<Block> &blocks() const { return blocks_; }
  IntrusiveList<Block> &blocks() { return blocks_; }

  const IntrusiveList<Local> &locals() const { return locals_; }
  IntrusiveList<Local> &locals() { return locals_; }

  void Dump() const;

  InsertPoint GetInsertPoint();
  void SetInsertPoint(const InsertPoint &point);

  // blocks
  Block *InsertBlock(Block *after);
  Block *AppendBlock();
  void RemoveBlock(Block *block);

  // context operations
  Value *LoadContext(size_t offset, ValueTy type);
  void StoreContext(size_t offset, Value *v, InstrFlag flags = IF_NONE);

  // local operations
  Value *LoadLocal(Local *local);
  void StoreLocal(Local *local, Value *v);

  // memory operations
  Value *Load(Value *addr, ValueTy type);
  void Store(Value *addr, Value *v);

  // cast / conversion operations
  Value *Cast(Value *v, ValueTy dest_type);
  Value *SExt(Value *v, ValueTy dest_type);
  Value *ZExt(Value *v, ValueTy dest_type);
  Value *Truncate(Value *v, ValueTy dest_type);

  // conditionals
  Value *Select(Value *cond, Value *t, Value *f);
  Value *EQ(Value *a, Value *b);
  Value *NE(Value *a, Value *b);
  Value *SGE(Value *a, Value *b);
  Value *SGT(Value *a, Value *b);
  Value *UGE(Value *a, Value *b);
  Value *UGT(Value *a, Value *b);
  Value *SLE(Value *a, Value *b);
  Value *SLT(Value *a, Value *b);
  Value *ULE(Value *a, Value *b);
  Value *ULT(Value *a, Value *b);

  // math operators
  Value *Add(Value *a, Value *b);
  Value *Sub(Value *a, Value *b);
  Value *SMul(Value *a, Value *b);
  Value *UMul(Value *a, Value *b);
  Value *Div(Value *a, Value *b);
  Value *Neg(Value *a);
  Value *Sqrt(Value *a);
  Value *Abs(Value *a);
  Value *Sin(Value *a);
  Value *Cos(Value *a);

  // bitwise operations
  Value *And(Value *a, Value *b);
  Value *Or(Value *a, Value *b);
  Value *Xor(Value *a, Value *b);
  Value *Not(Value *a);
  Value *Shl(Value *a, Value *n);
  Value *Shl(Value *a, int n);
  Value *AShr(Value *a, Value *n);
  Value *AShr(Value *a, int n);
  Value *LShr(Value *a, Value *n);
  Value *LShr(Value *a, int n);
  Value *AShd(Value *a, Value *n);
  Value *LShd(Value *a, Value *n);

  // branches
  void Branch(Value *dest);
  void BranchCond(Value *cond, Value *true_addr, Value *false_addr);

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
  Value *AllocDynamic(ValueTy type);
  Local *AllocLocal(ValueTy type);

 protected:
  Instr *AllocInstr(Opcode op, InstrFlag flags = IF_NONE);
  Instr *AppendInstr(Opcode op, InstrFlag flags = IF_NONE);

  Arena arena_;
  IntrusiveList<Block> blocks_;
  IntrusiveList<Local> locals_;
  Block *current_block_;
  Instr *current_instr_;
};
}
}
}

#endif
