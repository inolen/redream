#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include <unordered_map>
#include "core/core.h"

namespace dreavm {
namespace cpu {
namespace ir {

enum Opcode {
#define IR_OP(name) OP_##name,
#include "cpu/ir/ir_ops.inc"
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
  VALUE_F64,
  VALUE_BLOCK
};
enum {
  // not used by IRBuilder directly, but useful when generating lookup tables
  VALUE_V = 0,
  VALUE_NUM = VALUE_BLOCK + 1
};

enum {
  VALUE_I8_MASK = 1 << VALUE_I8,
  VALUE_I16_MASK = 1 << VALUE_I16,
  VALUE_I32_MASK = 1 << VALUE_I32,
  VALUE_I64_MASK = 1 << VALUE_I64,
  VALUE_F32_MASK = 1 << VALUE_F32,
  VALUE_F64_MASK = 1 << VALUE_F64,
  VALUE_BLOCK_MASK = 1 << VALUE_BLOCK,
  VALUE_INT_MASK = VALUE_I8_MASK | VALUE_I16_MASK | VALUE_I32_MASK |
                   VALUE_I64_MASK | VALUE_BLOCK_MASK,
  VALUE_FLOAT_MASK = VALUE_F32_MASK | VALUE_F64_MASK,
  VALUE_ALL_MASK = VALUE_I8_MASK | VALUE_I16_MASK | VALUE_I32_MASK |
                   VALUE_I64_MASK | VALUE_F32_MASK | VALUE_F64_MASK |
                   VALUE_BLOCK_MASK,
};
enum { NO_REGISTER = -1, NO_SLOT = -1 };

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
    case VALUE_BLOCK:
      return 4;
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
  Value(Block *v);

  ValueTy type() const { return type_; }

  bool constant() const { return constant_; }

  template <typename T>
  T value() const;

  const core::IntrusiveList<ValueRef> &refs() const { return refs_; }
  core::IntrusiveList<ValueRef> &refs() { return refs_; }

  int reg() const { return reg_; }
  void set_reg(int reg) { reg_ = reg; }

  int local() const { return local_; }
  void set_local(int local) { local_ = local; }

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
    Block *block_;
  };
  core::IntrusiveList<ValueRef> refs_;
  int reg_{NO_REGISTER};
  int local_{NO_SLOT};
  intptr_t tag_{0};
};

template <>
inline int8_t Value::value() const {
  DCHECK_EQ(type_, VALUE_I8);
  DCHECK_EQ(constant_, true);
  return i8_;
}
template <>
inline int16_t Value::value() const {
  DCHECK_EQ(type_, VALUE_I16);
  DCHECK_EQ(constant_, true);
  return i16_;
}
template <>
inline int32_t Value::value() const {
  DCHECK_EQ(type_, VALUE_I32);
  DCHECK_EQ(constant_, true);
  return i32_;
}
template <>
inline int64_t Value::value() const {
  DCHECK_EQ(type_, VALUE_I64);
  DCHECK_EQ(constant_, true);
  return i64_;
}
template <>
inline float Value::value() const {
  DCHECK_EQ(type_, VALUE_F32);
  DCHECK_EQ(constant_, true);
  return f32_;
}
template <>
inline double Value::value() const {
  DCHECK_EQ(type_, VALUE_F64);
  DCHECK_EQ(constant_, true);
  return f64_;
}
template <>
inline Block *Value::value() const {
  DCHECK_EQ(type_, VALUE_BLOCK);
  DCHECK_EQ(constant_, true);
  return block_;
}

// ValueRef is a layer of indirection between an instruction and a values it
// uses. Values maintain a list of all of their references, making it possible
// during optimization to replace all references to a value with a new value.
class ValueRef : public core::IntrusiveListNode<ValueRef> {
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

//
// instructions
//
enum InstrFlag { IF_NONE = 0x0, IF_INVALIDATE_CONTEXT = 0x1 };

class Instr : public core::IntrusiveListNode<Instr> {
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

  // temp debug variables
  intptr_t guest_addr;
  intptr_t guest_op;

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
class Edge : public core::IntrusiveListNode<Edge> {
 public:
  Edge(Block *src, Block *dst);

  Block *src() { return src_; }
  Block *dst() { return dst_; }

 private:
  Block *src_;
  Block *dst_;
};

class Block : public core::IntrusiveListNode<Block> {
  friend class IRBuilder;

 public:
  Block();
  ~Block();

  const core::IntrusiveList<Instr> &instrs() const { return instrs_; }
  core::IntrusiveList<Instr> &instrs() { return instrs_; }

  const core::IntrusiveList<Edge> &outgoing() const { return outgoing_; }
  core::IntrusiveList<Edge> &outgoing() { return outgoing_; }

  const core::IntrusiveList<Edge> &incoming() const { return incoming_; }
  core::IntrusiveList<Edge> &incoming() { return incoming_; }

  // filled in by the ControlFlowAnalysis pass, provides reverse postorder
  // iteration of blocks
  const Block *rpo_next() const { return rpo_next_; }
  Block *rpo_next() { return rpo_next_; }
  void set_rpo_next(Block *rpo_next) { rpo_next_ = rpo_next; }

  intptr_t tag() const { return tag_; }
  void set_tag(intptr_t tag) { tag_ = tag; }

  void AppendInstr(Instr *instr);
  void InsertInstr(Instr *after, Instr *instr);
  void ReplaceInstr(Instr *replace, Instr *with);
  void RemoveInstr(Instr *instr);

 private:
  core::IntrusiveList<Instr> instrs_;
  core::IntrusiveList<Edge> outgoing_;
  core::IntrusiveList<Edge> incoming_;
  Block *rpo_next_;
  intptr_t tag_;
};

//
// IRBuilder
//
typedef void (*ExternalFn)(void *);

enum MetadataTy { MD_GUEST_CYCLES, MD_NUM };

// Cache duplicate constant values. Constants aren't cached for memory purposes,
// they're cached to aid optimization passes. For example, duruing GVN constants
// will all share the same value number.
struct ConstantKey {
  ValueTy type;
  int64_t value;

  bool operator==(const ConstantKey &other) const {
    return type == other.type && value == other.value;
  }
};

class ConstantKeyHasher {
 public:
  size_t operator()(const ConstantKey &key) const {
    return std::hash<int64_t>()(key.value);
  }
};

typedef std::unordered_map<ConstantKey, Value *, ConstantKeyHasher> ConstantMap;

class IRBuilder {
 public:
  IRBuilder();

  const core::IntrusiveList<Block> &blocks() const { return blocks_; }
  core::IntrusiveList<Block> &blocks() { return blocks_; }

  int locals_size() const { return locals_size_; }

  static bool IsTerminator(const Instr *i);

  void Dump() const;

  // meta data
  void SetMetadata(MetadataTy type, Value *v);
  const Value *GetMetadata(MetadataTy type) const;

  // blocks
  void SetCurrentBlock(Block *block);
  Block *InsertBlock(Block *after);
  Block *AppendBlock();
  void RemoveBlock(Block *block);
  void AddEdge(Block *src, Block *dst);

  // instructions
  Instr *AllocInstr(Opcode op, InstrFlag flags = IF_NONE);

  // context operations
  Value *LoadContext(size_t offset, ValueTy type);
  void StoreContext(size_t offset, Value *v, InstrFlag flags = IF_NONE);

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

  // branches
  void Branch(Value *dest);
  void Branch(Block *dest);
  void BranchFalse(Value *cond, Value *dest);
  void BranchFalse(Value *cond, Block *dest);
  void BranchTrue(Value *cond, Value *dest);
  void BranchTrue(Value *cond, Block *dest);

  // calls
  void CallExternal(ExternalFn func);

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
  Value *AllocConstant(Block *c);
  Value *AllocDynamic(ValueTy type);
  int AllocLocal(ValueTy type);

 protected:
  Instr *AppendInstr(Opcode op, InstrFlag flags = IF_NONE);

  core::Arena arena_;
  core::IntrusiveList<Block> blocks_;
  Block *current_block_;
  ConstantMap constants_;
  int locals_size_;
  Value *metadata_[MD_NUM];
};
}
}
}

#endif
