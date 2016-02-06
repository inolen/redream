#include "emu/profiler.h"
#include "jit/ir/passes/register_allocation_pass.h"

using namespace re::jit::backend;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;

static inline int GetOrdinal(const Instr *i) { return (int)i->tag(); }

static inline void SetOrdinal(Instr *i, int ordinal) {
  i->set_tag((intptr_t)ordinal);
}

static inline bool RegisterCanStore(const Register &r, ValueTy type) {
  return r.value_types & (1 << type);
}

RegisterSet::RegisterSet(int max_registers) : live_(max_registers) {
  free_ = new int[max_registers];
}

RegisterSet::~RegisterSet() { delete[] free_; }

void RegisterSet::Clear() {
  num_free_ = 0;
  live_.Clear();
}

int RegisterSet::PopRegister() {
  if (!num_free_) {
    return NO_REGISTER;
  }
  return free_[--num_free_];
}

void RegisterSet::PushRegister(int reg) { free_[num_free_++] = reg; }

Interval *RegisterSet::HeadInterval() {
  if (live_.Empty()) {
    return nullptr;
  }

  return live_.front();
}

Interval *RegisterSet::TailInterval() {
  if (live_.Empty()) {
    return nullptr;
  }

  return live_.back();
}

void RegisterSet::PopHeadInterval() { live_.PopFront(); }

void RegisterSet::PopTailInterval() { live_.PopBack(); }

void RegisterSet::InsertInterval(Interval *interval) {
  auto it = std::lower_bound(live_.begin(), live_.end(),
                             GetOrdinal(interval->next->instr()),
                             [](const Interval *lhs, int rhs) {
                               return GetOrdinal(lhs->next->instr()) < rhs;
                             });

  live_.Insert(it, interval);
}

RegisterAllocationPass::RegisterAllocationPass(const Backend &backend)
    : int_registers_(backend.num_registers()),
      float_registers_(backend.num_registers()) {
  registers_ = backend.registers();
  num_registers_ = backend.num_registers();

  intervals_ = new Interval[num_registers_];
}

RegisterAllocationPass::~RegisterAllocationPass() { delete[] intervals_; }

void RegisterAllocationPass::Run(IRBuilder &builder) {
  PROFILER_RUNTIME("RegisterAllocationPass::Run");

  for (auto block : builder.blocks()) {
    Reset();

    AssignOrdinals(block);

    for (auto instr : block->instrs()) {
      Value *result = instr->result();

      // only allocate registers for results, assume constants can always be
      // encoded as immediates or that the backend has registers reserved
      // for storing the constants
      if (!result) {
        continue;
      }

      // sort the value's ref list
      result->refs().Sort([](const ValueRef *a, const ValueRef *b) {
        return GetOrdinal(a->instr()) < GetOrdinal(b->instr());
      });

      // get the live range of the value
      ValueRef *start = result->refs().head();
      ValueRef *end = result->refs().tail();

      // expire any old intervals, freeing up the registers they claimed
      ExpireOldIntervals(start->instr());

      // first, try and reuse the register of one of the incoming arguments
      int reg = ReuuseArgRegister(instr, start, end);
      if (reg == NO_REGISTER) {
        // else, allocate a new register for the result
        reg = AllocFreeRegister(result, start, end);
        if (reg == NO_REGISTER) {
          // if a register couldn't be allocated, spill a register and try again
          reg = AllocBlockedRegister(builder, result, start, end);
          CHECK_NE(reg, NO_REGISTER, "Failed to allocate register");
        }
      }

      result->set_reg(reg);
    }
  }
}

RegisterSet &RegisterAllocationPass::GetRegisterSet(ValueTy type) {
  if (IsIntType(type)) {
    return int_registers_;
  }

  if (IsFloatType(type)) {
    return float_registers_;
  }

  LOG_FATAL("Unexpected value type");
}

void RegisterAllocationPass::Reset() {
  int_registers_.Clear();
  float_registers_.Clear();

  for (int i = 0; i < num_registers_; i++) {
    const Register &r = registers_[i];

    if (r.value_types == VALUE_INT_MASK) {
      int_registers_.PushRegister(i);
    } else if (r.value_types == VALUE_FLOAT_MASK) {
      float_registers_.PushRegister(i);
    } else {
      LOG_FATAL(
          "Unsupported register value mask, expected VALUE_INT_MASK or "
          "VALUE_FLOAT_MASK");
    }
  }
}

void RegisterAllocationPass::AssignOrdinals(Block *block) {
  // assign each instruction an ordinal. these ordinals are used to describe
  // the live range of a particular value
  int ordinal = 0;
  for (auto instr : block->instrs()) {
    SetOrdinal(instr, ordinal);

    // space out ordinals to leave available values for instructions inserted
    // by AllocBlockedRegister. there should never be an ir op with more than
    // 10 arguments to spill registers for
    ordinal += 10;
  }
}

void RegisterAllocationPass::ExpireOldIntervals(Instr *start) {
  auto expire_set = [&](RegisterSet &set) {
    while (true) {
      Interval *interval = set.HeadInterval();
      if (!interval) {
        break;
      }

      // intervals are sorted by their next use, once one fails to expire or
      // advance, they all will
      if (GetOrdinal(interval->next->instr()) >= GetOrdinal(start)) {
        break;
      }

      // remove interval from the sorted set
      set.PopHeadInterval();

      // if there are no other uses, free the register assigned to this
      // interval
      if (!interval->next->next()) {
        set.PushRegister(interval->reg);
      }
      // if there are more uses, advance the next use and reinsert the interval
      // into the correct position
      else {
        interval->next = interval->next->next();
        set.InsertInterval(interval);
      }
    }
  };

  expire_set(int_registers_);
  expire_set(float_registers_);
}

// If the first argument isn't used after this instruction, its register
// can be reused to take advantage of many architectures supporting
// operations where the destination is the first argument.
// TODO could reorder arguments for communicative binary ops and do this
// with the second argument as well
int RegisterAllocationPass::ReuuseArgRegister(Instr *instr, ValueRef *start,
                                              ValueRef *end) {
  if (!instr->arg0() || instr->arg0()->constant()) {
    return NO_REGISTER;
  }

  int prefered = instr->arg0()->reg();
  if (prefered == NO_REGISTER) {
    return NO_REGISTER;
  }

  // make sure the register can hold the result type
  const Register &r = registers_[prefered];
  if (!RegisterCanStore(r, instr->result()->type())) {
    return NO_REGISTER;
  }

  // if the argument's register is used after this instruction, it's not
  // trivial to reuse
  Interval *interval = &intervals_[prefered];
  if (interval->next->next()) {
    return NO_REGISTER;
  }

  // the argument's register is not used after the current instruction, so the
  // register can be reused for the result. since the interval's current next
  // use (arg0 of this instruction) and the next use of the new interval  (the
  // result of this instruction) share the same ordinal, the interval can be
  // hijacked and overwritten without having to reinsert it into the register
  // set's sorted interval list
  CHECK_EQ(GetOrdinal(interval->next->instr()), GetOrdinal(start->instr()));
  interval->start = start;
  interval->next = start;
  interval->value = instr->result();
  interval->end = end;

  return prefered;
}

int RegisterAllocationPass::AllocFreeRegister(Value *value, ValueRef *start,
                                              ValueRef *end) {
  RegisterSet &set = GetRegisterSet(value->type());

  // get the first free register for this value type
  int reg = set.PopRegister();
  if (reg == NO_REGISTER) {
    return NO_REGISTER;
  }

  // add interval
  Interval *interval = &intervals_[reg];
  interval->value = value;
  interval->start = start;
  interval->end = end;
  interval->next = start;
  interval->reg = reg;
  set.InsertInterval(interval);

  return reg;
}

int RegisterAllocationPass::AllocBlockedRegister(IRBuilder &builder,
                                                 Value *value, ValueRef *start,
                                                 ValueRef *end) {
  InsertPoint insert_point = builder.GetInsertPoint();
  RegisterSet &set = GetRegisterSet(value->type());

  // spill the register who's next use is furthest away from start
  Interval *interval = set.TailInterval();
  set.PopTailInterval();

  // find the next and prev use of the register. the interval's value needs
  // to be spilled to the stack after the previous use, and filled back from
  // from the stack before it's next use
  ValueRef *next_ref = interval->next;
  ValueRef *prev_ref = next_ref->prev();
  CHECK(next_ref,
        "Register being spilled has no next use, why wasn't it expired?");
  CHECK(prev_ref,
        "Register being spilled has no prev use, why is it already live?");

  // allocate a place on the stack to spill the value
  Local *local = builder.AllocLocal(interval->value->type());

  // insert load before next use
  builder.SetInsertPoint({insert_point.block, next_ref->instr()->prev()});
  Value *load_local = builder.LoadLocal(local);
  Instr *load_instr = builder.GetInsertPoint().instr;

  // assign the load a valid ordinal
  int load_ordinal = GetOrdinal(load_instr->prev()) + 1;
  CHECK_LT(load_ordinal, GetOrdinal(load_instr->next()));
  SetOrdinal(load_instr, load_ordinal);

  // update references to interval->value after the next use to use the new
  // value filled from the stack. this code asssumes that the refs were
  // previously sorted inside of Run().
  while (next_ref) {
    // cache off next next since calling set_value will modify the linked list
    // pointers
    ValueRef *next_next_ref = next_ref->next();
    next_ref->set_value(load_local);
    next_ref = next_next_ref;
  }

  // with all references >= next_ref using the new value, prev_ref->next
  // should now be null
  CHECK(!prev_ref->next(), "All future references should have been replaced");

  // insert spill after prev use, note that order here is extremely important.
  // interval->value's ref list has already been sorted, and when the save
  // instruction is created and added as a reference, the sorted order will be
  // invalidated. because of this, the save instruction needs to be added after
  // the load instruction has updated the sorted references.
  builder.SetInsertPoint({insert_point.block, prev_ref->instr()});
  builder.StoreLocal(local, interval->value);
  Instr *store_instr = builder.GetInsertPoint().instr;

  // since the interval that this save belongs to has now expired, there's no
  // need to assign an ordinal to it

  // the new store should now be the final reference
  CHECK(prev_ref->next() && prev_ref->next()->instr() == store_instr,
        "Spill should be the final reference for the interval value");

  // overwrite the old interval
  interval->value = value;
  interval->start = start;
  interval->next = start;
  interval->end = end;
  set.InsertInterval(interval);

  // reset insert point
  builder.SetInsertPoint(insert_point);

  return interval->reg;
}
