#include <iomanip>
#include <sstream>
#include <unordered_map>
#include "cpu/ir/ir_builder.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::ir;

const char *dreavm::cpu::ir::Opnames[NUM_OPCODES] = {
#define IR_OP(name) #name,
#include "cpu/ir/ir_ops.inc"
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
Value::Value(Block *v) : type_(VALUE_BLOCK), constant_(true), block_(v) {}

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
      return *reinterpret_cast<const uint32_t *>(&f32_);
    case VALUE_F64:
      return *reinterpret_cast<const uint64_t *>(&f64_);
    case VALUE_BLOCK:
      return reinterpret_cast<intptr_t>(block_);
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

//
// Instr
//
Instr::Instr(Opcode op, InstrFlag flags)
    : block_(nullptr),
      op_(op),
      flags_(flags),
      args_{{this}, {this}, {this}, {this}},
      tag_(0) {}

Instr::~Instr() {}

void Instr::MoveAfter(Instr *other) {
  block_->UnlinkInstr(this);
  other->block_->InsertInstr(other, this);
}

//
// Block
//
Edge::Edge(Block *src, Block *dst) : src_(src), dst_(dst) {}

Block::Block() : rpo_next_(nullptr) {}
Block::~Block() {
  while (instrs_.tail()) {
    RemoveInstr(instrs_.tail());
  }
}

void Block::AppendInstr(Instr *instr) {
  instr->set_block(this);
  instrs_.Append(instr);
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
    : arena_(1024),
      current_block_(nullptr),
      locals_size_(0),
      guest_cycles_(0) {}

bool IRBuilder::IsTerminator(const Instr *i) {
  return i->op() == OP_BRANCH || i->op() == OP_BRANCH_COND;
}

// TODO clean and speed up?
void IRBuilder::Dump() const {
  std::unordered_map<intptr_t, std::string> block_vars;
  std::unordered_map<intptr_t, std::string> value_vars;
  int next_temp_id = 0;
  auto DumpBlock = [&](std::stringstream &ss, const Block *b) {
    auto it = block_vars.find((intptr_t)b);
    ss << it->second;
  };
  auto DumpVariable = [&](std::stringstream &ss, const Value *v) {
    if (!v) {
      return;
    }
    auto it = value_vars.find((intptr_t)v);
    if (it == value_vars.end()) {
      std::string name = "%" + std::to_string(next_temp_id++);
      auto res = value_vars.insert(std::make_pair((intptr_t)v, name));
      it = res.first;
    }
    ss << it->second << " (" << v->reg() << ")";
  };
  auto DumpValue = [&](std::stringstream &ss, const Value *v) {
    if (!v) {
      return;
    } else if (!v->constant()) {
      DumpVariable(ss, v);
    } else {
      switch (v->type()) {
        case VALUE_I8:
          ss << v->value<int8_t>();
          break;
        case VALUE_I16:
          ss << v->value<int16_t>();
          break;
        case VALUE_I32:
          ss << v->value<int32_t>();
          break;
        case VALUE_I64:
          ss << v->value<int64_t>();
          break;
        case VALUE_F32:
          ss << v->value<float>();
          break;
        case VALUE_F64:
          ss << v->value<double>();
          break;
        case VALUE_BLOCK:
          DumpBlock(ss, v->value<Block *>());
          break;
      }
    }
    ss << " ";
  };
  int bc = 0;
  int ic = 0;
  for (auto block : blocks_) {
    block_vars.insert(
        std::make_pair((intptr_t)block, "blk" + std::to_string(bc++)));
  }
  for (auto block : blocks_) {
    std::stringstream header;
    DumpBlock(header, block);
    header << ":" << std::endl;
    LOG(INFO) << header.str();

    for (auto instr : block->instrs()) {
      std::stringstream ss;
      ss << ic++ << ". " << Opnames[instr->op()] << " ";
      DumpValue(ss, instr->arg0());
      DumpValue(ss, instr->arg1());
      DumpValue(ss, instr->arg2());
      DumpValue(ss, instr->result());
      LOG(INFO) << ss.str();
    }
    LOG(INFO) << std::endl;
  }
}

Block *IRBuilder::GetCurrentBlock() { return current_block_; }
void IRBuilder::SetCurrentBlock(Block *block) { current_block_ = block; }

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
  }

  blocks_.Remove(block);

  // call destructor manually
  block->~Block();
}

void IRBuilder::AddEdge(Block *src, Block *dst) {
  CHECK_NE(src, dst);

  // linked list data is intrusive, need to allocate two edge objects
  {
    Edge *edge = arena_.Alloc<Edge>();
    new (edge) Edge(src, dst);
    src->outgoing().Append(edge);
  }
  {
    Edge *edge = arena_.Alloc<Edge>();
    new (edge) Edge(src, dst);
    dst->incoming().Append(edge);
  }
}

Value *IRBuilder::LoadContext(size_t offset, ValueTy type) {
  Instr *instr = AppendInstr(OP_LOAD_CONTEXT);
  Value *result = AllocDynamic(type);
  instr->set_arg0(AllocConstant((int32_t)offset));
  instr->set_result(result);
  return result;
}

void IRBuilder::StoreContext(size_t offset, Value *v, InstrFlag flags) {
  Instr *instr = AppendInstr(OP_STORE_CONTEXT, flags);
  instr->set_arg0(AllocConstant((int32_t)offset));
  instr->set_arg1(v);
}

Value *IRBuilder::LoadLocal(size_t offset, ValueTy type) {
  Instr *instr = AppendInstr(OP_LOAD_LOCAL);
  Value *result = AllocDynamic(type);
  instr->set_arg0(AllocConstant((int32_t)offset));
  instr->set_result(result);
  return result;
}

void IRBuilder::StoreLocal(size_t offset, Value *v) {
  Instr *instr = AppendInstr(OP_STORE_LOCAL);
  instr->set_arg0(AllocConstant((int32_t)offset));
  instr->set_arg1(v);
}

Value *IRBuilder::Load(Value *addr, ValueTy type) {
  CHECK_EQ(VALUE_I32, addr->type());

  Instr *instr = AppendInstr(OP_LOAD);
  Value *result = AllocDynamic(type);
  instr->set_arg0(addr);
  instr->set_result(result);
  return result;
}

void IRBuilder::Store(Value *addr, Value *v) {
  CHECK_EQ(VALUE_I32, addr->type());

  Instr *instr = AppendInstr(OP_STORE);
  instr->set_arg0(addr);
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

Value *IRBuilder::Sin(Value *a) {
  Instr *instr = AppendInstr(OP_SIN);
  Value *result = AllocDynamic(a->type());
  instr->set_arg0(a);
  instr->set_result(result);
  return result;
}

Value *IRBuilder::Cos(Value *a) {
  Instr *instr = AppendInstr(OP_COS);
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

void IRBuilder::Branch(Value *dest) {
  Instr *instr = AppendInstr(OP_BRANCH);
  instr->set_arg0(dest);
}

void IRBuilder::Branch(Block *dest) {
  Instr *instr = AppendInstr(OP_BRANCH);
  instr->set_arg0(AllocConstant(dest));
}

void IRBuilder::BranchFalse(Value *cond, Value *false_addr) {
  if (cond->type() != VALUE_I8) {
    cond = NE(cond, AllocConstant(0));
  }

  // create fallthrough block automatically
  Block *true_block = InsertBlock(current_block_);

  Instr *instr = AppendInstr(OP_BRANCH_COND);
  instr->set_arg0(cond);
  instr->set_arg1(AllocConstant(true_block));
  instr->set_arg2(false_addr);

  SetCurrentBlock(true_block);
}

void IRBuilder::BranchFalse(Value *cond, Block *false_block) {
  if (cond->type() != VALUE_I8) {
    cond = NE(cond, AllocConstant(0));
  }

  // create fallthrough block automatically
  Block *true_block = InsertBlock(current_block_);

  Instr *instr = AppendInstr(OP_BRANCH_COND);
  instr->set_arg0(cond);
  instr->set_arg1(AllocConstant(true_block));
  instr->set_arg2(AllocConstant(false_block));

  SetCurrentBlock(true_block);
}

void IRBuilder::BranchTrue(Value *cond, Value *true_addr) {
  if (cond->type() != VALUE_I8) {
    cond = NE(cond, AllocConstant(0));
  }

  // create fallthrough block automatically
  Block *false_block = InsertBlock(current_block_);

  Instr *instr = AppendInstr(OP_BRANCH_COND);
  instr->set_arg0(cond);
  instr->set_arg1(true_addr);
  instr->set_arg2(AllocConstant(false_block));

  SetCurrentBlock(false_block);
}

void IRBuilder::BranchTrue(Value *cond, Block *true_block) {
  if (cond->type() != VALUE_I8) {
    cond = NE(cond, AllocConstant(0));
  }

  // create fallthrough block automatically
  Block *false_block = InsertBlock(current_block_);

  Instr *instr = AppendInstr(OP_BRANCH_COND);
  instr->set_arg0(cond);
  instr->set_arg1(AllocConstant(true_block));
  instr->set_arg2(AllocConstant(false_block));

  SetCurrentBlock(false_block);
}

void IRBuilder::BranchCond(Value *cond, Block *true_block, Block *false_block) {
  if (cond->type() != VALUE_I8) {
    cond = NE(cond, AllocConstant(0));
  }

  Instr *instr = AppendInstr(OP_BRANCH_COND);
  instr->set_arg0(cond);
  instr->set_arg1(AllocConstant(true_block));
  instr->set_arg2(AllocConstant(false_block));

  SetCurrentBlock(false_block);
}

void IRBuilder::CallExternal(ExternalFn func) {
  Instr *instr = AppendInstr(OP_CALL_EXTERNAL, IF_INVALIDATE_CONTEXT);
  instr->set_arg0(AllocConstant((uint64_t)(intptr_t)func));
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

Value *IRBuilder::AllocConstant(Block *c) {
  Value *v = arena_.Alloc<Value>();
  new (v) Value(c);
  return v;
}

Value *IRBuilder::AllocDynamic(ValueTy type) {
  Value *v = arena_.Alloc<Value>();
  new (v) Value(type);
  return v;
}

int IRBuilder::AllocLocal(ValueTy type) {
  int offset = locals_size_;
  locals_size_ += SizeForType(type);
  return offset;
}

Instr *IRBuilder::AllocInstr(Opcode op, InstrFlag flags) {
  Instr *instr = arena_.Alloc<Instr>();
  new (instr) Instr(op, flags);
  return instr;
}

Instr *IRBuilder::AppendInstr(Opcode op, InstrFlag flags) {
  if (!current_block_ || (current_block_->instrs().tail() &&
                          IsTerminator(current_block_->instrs().tail()))) {
    current_block_ = InsertBlock(current_block_);
  }

  Instr *instr = AllocInstr(op, flags);
  current_block_->AppendInstr(instr);
  return instr;
}
