#include <sstream>
#include "core/memory.h"
#include "jit/ir/ir_builder.h"
#include "jit/ir/ir_writer.h"

using namespace re::jit;
using namespace re::jit::ir;

const char *re::jit::ir::Opnames[NUM_OPS] = {
#define IR_OP(name) #name,
#include "jit/ir/ir_ops.inc"
};

//
// Value
//
Value::Value(ValueTy ty) : type_(ty), constant_(false) {}
Value::Value(int8_t v) : type_(VALUE_I8), constant_(true), i8_(v) {}
Value::Value(int16_t v) : type_(VALUE_I16), constant_(true), i16_(v) {}
Value::Value(int32_t v) : type_(VALUE_I32), constant_(true), i32_(v) {}
Value::Value(int64_t v) : type_(VALUE_I64), constant_(true), i64_(v) {}
Value::Value(float v) : type_(VALUE_F32), constant_(true), f32_(v) {}
Value::Value(double v) : type_(VALUE_F64), constant_(true), f64_(v) {}

uint64_t Value::GetZExtValue() const {
  switch (type_) {
    case VALUE_I8:
      return static_cast<uint8_t>(i8_);
    case VALUE_I16:
      return static_cast<uint16_t>(i16_);
    case VALUE_I32:
      return static_cast<uint32_t>(i32_);
    case VALUE_I64:
      return static_cast<uint64_t>(i64_);
    case VALUE_F32:
      return re::load<uint32_t>(&f32_);
    case VALUE_F64:
      return re::load<uint64_t>(&f64_);
  }
  return 0;
}

void Value::AddRef(ValueRef *ref) { refs_.Append(ref); }

void Value::RemoveRef(ValueRef *ref) { refs_.Remove(ref); }

void Value::ReplaceRefsWith(Value *other) {
  CHECK_NE(this, other);

  // NOTE set_value will modify refs, be careful iterating
  auto it = refs_.begin();
  while (it != refs_.end()) {
    ValueRef *ref = *(it++);
    ref->set_value(other);
  }
}

ValueRef::ValueRef(Instr *instr) : instr_(instr), value_(nullptr) {}

ValueRef::~ValueRef() {
  if (value_) {
    value_->RemoveRef(this);
  }
}

Local::Local(ValueTy ty, Value *offset) : type_(ty), offset_(offset) {}

//
// Instr
//
Instr::Instr(Op op)
    : block_(nullptr),
      op_(op),
      args_{{this}, {this}, {this}, {this}},
      tag_(0) {}

Instr::~Instr() {}

//
// Block
//
Block::Block() : tag_(0) {}
Block::~Block() {
  while (instrs_.tail()) {
    RemoveInstr(instrs_.tail());
  }
}

void Block::InsertInstr(Instr *after, Instr *instr) {
  instr->set_block(this);
  instrs_.Insert(after, instr);
}

void Block::ReplaceInstr(Instr *replace, Instr *with) {
  // insert the new instruction
  InsertInstr(replace, with);

  // replace references to our result with other result
  if (replace->result()) {
    CHECK_NOTNULL(with->result());
    replace->result()->ReplaceRefsWith(with->result());
  }

  // remove old instruction
  RemoveInstr(replace);
}

void Block::RemoveInstr(Instr *instr) {
  instr->set_block(nullptr);
  instrs_.Remove(instr);

  // call destructor manually
  instr->~Instr();
}

void Block::UnlinkInstr(Instr *instr) {
  instr->set_block(nullptr);
  instrs_.Remove(instr);
}

//
// IRBuilder
//
IRBuilder::IRBuilder()
    : arena_(1024), current_block_(nullptr), current_instr_(nullptr) {}

void IRBuilder::Dump() const {
  IRWriter writer;
  std::ostringstream ss;
  writer.Print(*this, ss);
  LOG_INFO(ss.str().c_str());
}

InsertPoint IRBuilder::GetInsertPoint() {
  return {current_block_, current_instr_};
}

void IRBuilder::SetInsertPoint(const InsertPoint &point) {
  current_block_ = point.block;
  current_instr_ = point.instr;
}

Block *IRBuilder::InsertBlock(Block *after) {
  Block *block = arena_.Alloc<Block>();
  new (block) Block();

  // insert at beginning if no after specified
  if (!after) {
    blocks_.Insert(nullptr, block);
  } else {
    blocks_.Insert(after, block);
  }

  return block;
}

Block *IRBuilder::AppendBlock() { return InsertBlock(blocks_.tail()); }

void IRBuilder::RemoveBlock(Block *block) {
  if (current_block_ == block) {
    current_block_ = block->next() ? block->next() : block->prev();
    current_instr_ = current_block_->instrs().tail();
  }

  blocks_.Remove(block);

  // call destructor manually
  block->~Block();
}

Value *IRBuilder::LoadHost(Value *addr, ValueTy type) {
  CHECK_EQ(VALUE_I64, addr->type());

  Instr *instr = AppendInstr(OP_LOAD_HOST);
  Value *result = AllocDynamic(type);
  instr->set_arg0(addr);
  instr->set_result(result);
  return result;
}

void IRBuilder::StoreHost(Value *addr, Value *v) {
  CHECK_EQ(VALUE_I64, addr->type());

  Instr *instr = AppendInstr(OP_STORE_HOST);
  instr->set_arg0(addr);
  instr->set_arg1(v);
}

Value *IRBuilder::LoadGuest(Value *addr, ValueTy type) {
  CHECK_EQ(VALUE_I32, addr->type());

  Instr *instr = AppendInstr(OP_LOAD_GUEST);
  Value *result = AllocDynamic(type);
  instr->set_arg0(addr);
  instr->set_result(result);
  return result;
}

void IRBuilder::StoreGuest(Value *addr, Value *v) {
  CHECK_EQ(VALUE_I32, addr->type());

  Instr *instr = AppendInstr(OP_STORE_GUEST);
  instr->set_arg0(addr);
  instr->set_arg1(v);
}

Value *IRBuilder::LoadContext(size_t offset, ValueTy type) {
  Instr *instr = AppendInstr(OP_LOAD_CONTEXT);
  Value *result = AllocDynamic(type);
  instr->set_arg0(AllocConstant((int32_t)offset));
  instr->set_result(result);
  return result;
}

void IRBuilder::StoreContext(size_t offset, Value *v) {
  Instr *instr = AppendInstr(OP_STORE_CONTEXT);
  instr->set_arg0(AllocConstant((int32_t)offset));
  instr->set_arg1(v);
}

Value *IRBuilder::LoadLocal(Local *local) {
  Instr *instr = AppendInstr(OP_LOAD_LOCAL);
  Value *result = AllocDynamic(local->type());
  instr->set_arg0(local->offset());
  instr->set_result(result);
  return result;
}

void IRBuilder::StoreLocal(Local *local, Value *v) {
  Instr *instr = AppendInstr(OP_STORE_LOCAL);
  instr->set_arg0(local->offset());
  instr->set_arg1(v);
}

Value *IRBuilder::Cast(Value *v, ValueTy dest_type) {
  CHECK((IsIntType(v->type()) && IsFloatType(dest_type)) ||
        (IsFloatType(v->type()) && IsIntType(dest_type)));

  Instr *instr = AppendInstr(OP_CAST);
  Value *result = AllocDynamic(dest_type);
  instr->set_arg0(v);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::SExt(Value *v, ValueTy dest_type) {
  CHECK(IsIntType(v->type()) && IsIntType(dest_type));

  Instr *instr = AppendInstr(OP_SEXT);
  Value *result = AllocDynamic(dest_type);
  instr->set_arg0(v);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::ZExt(Value *v, ValueTy dest_type) {
  CHECK(IsIntType(v->type()) && IsIntType(dest_type));

  Instr *instr = AppendInstr(OP_ZEXT);
  Value *result = AllocDynamic(dest_type);
  instr->set_arg0(v);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Truncate(Value *v, ValueTy dest_type) {
  CHECK(IsIntType(v->type()) && IsIntType(dest_type));

  Instr *instr = AppendInstr(OP_TRUNCATE);
  Value *result = AllocDynamic(dest_type);
  instr->set_arg0(v);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Select(Value *cond, Value *t, Value *f) {
  CHECK_EQ(t->type(), f->type());

  if (cond->type() != VALUE_I8) {
    cond = NE(cond, AllocConstant(0));
  }

  Instr *instr = AppendInstr(OP_SELECT);
  Value *result = AllocDynamic(t->type());
  instr->set_arg0(cond);
  instr->set_arg1(t);
  instr->set_arg2(f);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::EQ(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_EQ);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::NE(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_NE);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::SGE(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_SGE);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::SGT(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_SGT);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::UGE(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());
  CHECK_EQ(true, IsIntType(a->type()) && IsIntType(b->type()));

  Instr *instr = AppendInstr(OP_UGE);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::UGT(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());
  CHECK_EQ(true, IsIntType(a->type()) && IsIntType(b->type()));

  Instr *instr = AppendInstr(OP_UGT);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::SLE(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_SLE);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::SLT(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_SLT);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::ULE(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());
  CHECK_EQ(true, IsIntType(a->type()) && IsIntType(b->type()));

  Instr *instr = AppendInstr(OP_ULE);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::ULT(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());
  CHECK_EQ(true, IsIntType(a->type()) && IsIntType(b->type()));

  Instr *instr = AppendInstr(OP_ULT);
  Value *result = AllocDynamic(VALUE_I8);
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Add(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_ADD);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Sub(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_SUB);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::SMul(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_SMUL);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::UMul(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  CHECK(IsIntType(a->type()));
  Instr *instr = AppendInstr(OP_UMUL);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Div(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_DIV);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Neg(Value *a) {
  Instr *instr = AppendInstr(OP_NEG);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Sqrt(Value *a) {
  Instr *instr = AppendInstr(OP_SQRT);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Abs(Value *a) {
  Instr *instr = AppendInstr(OP_ABS);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::And(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_AND);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Or(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_OR);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Xor(Value *a, Value *b) {
  CHECK_EQ(a->type(), b->type());

  Instr *instr = AppendInstr(OP_XOR);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(b);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Not(Value *a) {
  Instr *instr = AppendInstr(OP_NOT);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Shl(Value *a, Value *n) {
  CHECK_EQ(VALUE_I32, n->type());

  Instr *instr = AppendInstr(OP_SHL);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(n);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Shl(Value *a, int n) {
  return Shl(a, AllocConstant((int32_t)n));
}

Value *IRBuilder::AShr(Value *a, Value *n) {
  CHECK_EQ(VALUE_I32, n->type());

  Instr *instr = AppendInstr(OP_ASHR);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(n);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::AShr(Value *a, int n) {
  return AShr(a, AllocConstant((int32_t)n));
}

Value *IRBuilder::LShr(Value *a, Value *n) {
  CHECK_EQ(VALUE_I32, n->type());

  Instr *instr = AppendInstr(OP_LSHR);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(n);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::LShr(Value *a, int n) {
  return LShr(a, AllocConstant((int32_t)n));
}

Value *IRBuilder::AShd(Value *a, Value *n) {
  CHECK_EQ(VALUE_I32, a->type());
  CHECK_EQ(VALUE_I32, n->type());

  Instr *instr = AppendInstr(OP_ASHD);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(n);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::LShd(Value *a, Value *n) {
  CHECK_EQ(VALUE_I32, a->type());
  CHECK_EQ(VALUE_I32, n->type());

  Instr *instr = AppendInstr(OP_LSHD);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_arg1(n);
  instr->set_result(result);
  return result;
}

void IRBuilder::Branch(Value *dest) {
  Instr *instr = AppendInstr(OP_BRANCH);
  instr->set_arg0(dest);
}

void IRBuilder::BranchCond(Value *cond, Value *true_addr, Value *false_addr) {
  if (cond->type() != VALUE_I8) {
    cond = NE(cond, AllocConstant(0));
  }

  Instr *instr = AppendInstr(OP_BRANCH_COND);
  instr->set_arg0(cond);
  instr->set_arg1(true_addr);
  instr->set_arg2(false_addr);
}

void IRBuilder::CallExternal1(Value *addr) {
  CHECK_EQ(addr->type(), VALUE_I64);

  Instr *instr = AppendInstr(OP_CALL_EXTERNAL);
  instr->set_arg0(addr);
}

void IRBuilder::CallExternal2(Value *addr, Value *arg0) {
  CHECK_EQ(addr->type(), VALUE_I64);
  CHECK_EQ(arg0->type(), VALUE_I64);

  Instr *instr = AppendInstr(OP_CALL_EXTERNAL);
  instr->set_arg0(addr);
  instr->set_arg1(arg0);
}

void IRBuilder::GuestAddress(uint32_t guest_addr) {
  Instr *instr = AppendInstr(OP_GUEST_ADDRESS);
  instr->set_arg0(AllocConstant(guest_addr));
}

Value *IRBuilder::AllocConstant(uint8_t c) { return AllocConstant((int8_t)c); }

Value *IRBuilder::AllocConstant(uint16_t c) {
  return AllocConstant((int16_t)c);
}

Value *IRBuilder::AllocConstant(uint32_t c) {
  return AllocConstant((int32_t)c);
}

Value *IRBuilder::AllocConstant(uint64_t c) {
  return AllocConstant((int64_t)c);
}

Value *IRBuilder::AllocConstant(int8_t c) {
  Value *v = arena_.Alloc<Value>();
  new (v) Value(c);
  return v;
}

Value *IRBuilder::AllocConstant(int16_t c) {
  Value *v = arena_.Alloc<Value>();
  new (v) Value(c);
  return v;
}

Value *IRBuilder::AllocConstant(int32_t c) {
  Value *v = arena_.Alloc<Value>();
  new (v) Value(c);
  return v;
}

Value *IRBuilder::AllocConstant(int64_t c) {
  Value *v = arena_.Alloc<Value>();
  new (v) Value(c);
  return v;
}

Value *IRBuilder::AllocConstant(float c) {
  Value *v = arena_.Alloc<Value>();
  new (v) Value(c);
  return v;
}

Value *IRBuilder::AllocConstant(double c) {
  Value *v = arena_.Alloc<Value>();
  new (v) Value(c);
  return v;
}

Value *IRBuilder::AllocDynamic(ValueTy type) {
  Value *v = arena_.Alloc<Value>();
  new (v) Value(type);
  return v;
}

Local *IRBuilder::AllocLocal(ValueTy type) {
  Local *l = arena_.Alloc<Local>();
  new (l) Local(type, AllocConstant(0));
  locals_.Append(l);
  return l;
}

Instr *IRBuilder::AllocInstr(Op op) {
  Instr *instr = arena_.Alloc<Instr>();
  new (instr) Instr(op);
  return instr;
}

Instr *IRBuilder::AppendInstr(Op op) {
  if (!current_block_) {
    current_block_ = InsertBlock(current_block_);
    current_instr_ = nullptr;
  }

  Instr *instr = AllocInstr(op);
  current_block_->InsertInstr(current_instr_, instr);
  current_instr_ = instr;
  return instr;
}
