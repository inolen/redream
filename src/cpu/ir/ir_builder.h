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
  VALUE_I8,
  VALUE_I16,
  VALUE_I32,
  VALUE_I64,
  VALUE_F32,
  VALUE_F64,
  VALUE_BLOCK
};
enum { VALUE_NUM = VALUE_BLOCK + 1 };

class Block;
class Instr;
class ValueRef;

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
  intptr_t tag_;
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
  ValueRef();

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

  intptr_t tag() const { return tag_; }
  void set_tag(intptr_t tag) { tag_ = tag; }

  void ReplaceWith(Instr *other);
  void Remove();

  // temp debug variables
  intptr_t guest_addr;
  intptr_t guest_op;

 private:
  Block *set_block(Block *block) { return block_ = block; }

  const Value *arg(int i) const { return args_[i].value(); }
  Value *arg(int i) { return args_[i].value(); }
  void set_arg(int i, Value *v) { args_[i].set_value(v); }

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

  int id() const { return id_; }

  const core::IntrusiveList<Instr> &instrs() const { return instrs_; }
  core::IntrusiveList<Instr> &instrs() { return instrs_; }

  const core::IntrusiveList<Edge> &outgoing_edges() const {
    return outgoing_edges_;
  }
  core::IntrusiveList<Edge> &outgoing_edges() { return outgoing_edges_; }

  const core::IntrusiveList<Edge> &incoming_edges() const {
    return incoming_edges_;
  }
  core::IntrusiveList<Edge> &incoming_edges() { return incoming_edges_; }

  intptr_t tag() const { return tag_; }
  void set_tag(intptr_t tag) { tag_ = tag; }

  void AppendInstr(Instr *instr);
  void InsertInstr(Instr *after, Instr *instr);
  void RemoveInstr(Instr *instr);

 private:
  void set_id(int id) { id_ = id; }

  int id_;
  core::IntrusiveList<Instr> instrs_;
  core::IntrusiveList<Edge> outgoing_edges_;
  core::IntrusiveList<Edge> incoming_edges_;
  intptr_t tag_;
};

//
// IRBuilder
//
typedef void (*ExternalFn)(void *);

enum MetadataTy { MD_GUEST_CYCLES, MD_NUM };

// Cache duplicate constant values
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

  // debug
  void Printf(Value *v);

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
  void BranchIndirect(Value *addr);

  // calls
  void CallExternal(ExternalFn func);

  // values
  Value *AllocDynamic(ValueTy type);
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

 protected:
  Instr *AppendInstr(Opcode op, InstrFlag flags = IF_NONE);

  core::Arena arena_;
  core::IntrusiveList<Block> blocks_;
  Block *current_block_;
  ConstantMap constants_;
  Value *metadata_[MD_NUM];
};
}
}
}

#endif
