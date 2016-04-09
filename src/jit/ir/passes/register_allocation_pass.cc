#include "core/minmax_heap.h"
#include "jit/ir/passes/register_allocation_pass.h"

using namespace re::jit::backend;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;

static inline int GetOrdinal(const Instr *i) { return (int)i->tag(); }

static inline void SetOrdinal(Instr *i, int ordinal) {
  i->set_tag((intptr_t)ordinal);
}

static inline bool RegisterCanStore(const Register &r, ValueType type) {
  return r.value_types & (1 << type);
}

struct LiveIntervalSort {
  bool operator()(const Interval *lhs, const Interval *rhs) const {
    return !lhs->next ||
           GetOrdinal(lhs->next->instr()) < GetOrdinal(rhs->next->instr());
  }
};

RegisterSet::RegisterSet(int max_registers) {
  free_ = new int[max_registers];
  live_ = new Interval *[max_registers];
}

RegisterSet::~RegisterSet() {
  delete[] free_;
  delete[] live_;
}

void RegisterSet::Clear() {
  num_free_ = 0;
  num_live_ = 0;
}

int RegisterSet::PopRegister() {
  if (!num_free_) {
    return NO_REGISTER;
  }
  return free_[--num_free_];
}

void RegisterSet::PushRegister(int reg) { free_[num_free_++] = reg; }

Interval *RegisterSet::HeadInterval() {
  if (!num_live_) {
    return nullptr;
  }

  auto it = re::mmheap_find_min(live_, live_ + num_live_, LiveIntervalSort());
  return *it;
}

Interval *RegisterSet::TailInterval() {
  if (!num_live_) {
    return nullptr;
  }

  auto it = re::mmheap_find_max(live_, live_ + num_live_, LiveIntervalSort());
  return *it;
}

void RegisterSet::PopHeadInterval() {
  re::mmheap_pop_min(live_, live_ + num_live_, LiveIntervalSort());
  num_live_--;
}

void RegisterSet::PopTailInterval() {
  re::mmheap_pop_max(live_, live_ + num_live_, LiveIntervalSort());
  num_live_--;
}

void RegisterSet::InsertInterval(Interval *interval) {
  live_[num_live_++] = interval;
  re::mmheap_push(live_, live_ + num_live_, LiveIntervalSort());
}

RegisterAllocationPass::RegisterAllocationPass(const Backend &backend)
    : int_registers_(backend.num_registers()),
      float_registers_(backend.num_registers()),
      vector_registers_(backend.num_registers()) {
  registers_ = backend.registers();
  num_registers_ = backend.num_registers();

  intervals_ = new Interval[num_registers_];
}

RegisterAllocationPass::~RegisterAllocationPass() { delete[] intervals_; }

void RegisterAllocationPass::Run(IRBuilder &builder, bool debug) {
  Reset();

  AssignOrdinals(builder);

  for (auto instr : builder.instrs()) {
    // only allocate registers for results, assume constants can always be
    // encoded as immediates or that the backend has registers reserved
    // for storing the constants
    if (instr->type() == VALUE_V) {
      continue;
    }

    // sort the instruction's ref list
    instr->uses().Sort([](const Use *a, const Use *b) {
      return GetOrdinal(a->instr()) < GetOrdinal(b->instr());
    });

    // expire any old intervals, freeing up the registers they claimed
    ExpireOldIntervals(instr);

    // first, try and reuse the register of one of the incoming arguments
    int reg = ReuseArgRegister(builder, instr);
    if (reg == NO_REGISTER) {
      // else, allocate a new register for the result
      reg = AllocFreeRegister(instr);
      if (reg == NO_REGISTER) {
        // if a register couldn't be allocated, spill a register and try again
        reg = AllocBlockedRegister(builder, instr);
        CHECK_NE(reg, NO_REGISTER, "Failed to allocate register");
      }
    }

    instr->set_reg(reg);
  }
}

RegisterSet &RegisterAllocationPass::GetRegisterSet(ValueType type) {
  if (IsIntType(type)) {
    return int_registers_;
  }

  if (IsFloatType(type)) {
    return float_registers_;
  }

  if (IsVectorType(type)) {
    return vector_registers_;
  }

  LOG_FATAL("Unexpected value type");
}

void RegisterAllocationPass::Reset() {
  int_registers_.Clear();
  float_registers_.Clear();
  vector_registers_.Clear();

  for (int i = 0; i < num_registers_; i++) {
    const Register &r = registers_[i];

    if (r.value_types == VALUE_INT_MASK) {
      int_registers_.PushRegister(i);
    } else if (r.value_types == VALUE_FLOAT_MASK) {
      float_registers_.PushRegister(i);
    } else if (r.value_types == VALUE_VECTOR_MASK) {
      vector_registers_.PushRegister(i);
    } else {
      LOG_FATAL("Unsupported register value mask");
    }
  }
}

void RegisterAllocationPass::AssignOrdinals(IRBuilder &builder) {
  // assign each instruction an ordinal. these ordinals are used to describe
  // the live range of a particular value
  int ordinal = 0;
  for (auto instr : builder.instrs()) {
    SetOrdinal(instr, ordinal);

    // space out ordinals to leave available values for instructions inserted
    // by AllocBlockedRegister. there should never be an ir op with more than
    // 10 arguments to spill registers for
    ordinal += 10;
  }
}

void RegisterAllocationPass::ExpireOldIntervals(Instr *instr) {
  auto expire_set = [&](RegisterSet &set) {
    while (true) {
      Interval *interval = set.HeadInterval();
      if (!interval) {
        break;
      }

      // intervals are sorted by their next use, once one fails to expire or
      // advance, they all will
      if (interval->next &&
          GetOrdinal(interval->next->instr()) >= GetOrdinal(instr)) {
        break;
      }

      // remove interval from the sorted set
      set.PopHeadInterval();

      // if there are more uses, advance the next use and reinsert the interval
      // into the correct position
      if (interval->next && interval->next->next()) {
        interval->next = interval->next->next();
        set.InsertInterval(interval);
      }
      // if there are no more uses, but the register has been reused by
      // ReuseArgRegister, requeue the interval at this time
      else if (interval->reused) {
        Instr *reused = interval->reused;
        interval->instr = reused;
        interval->reused = nullptr;
        interval->start = reused->uses().head();
        interval->end = reused->uses().tail();
        interval->next = interval->start;
        set.InsertInterval(interval);
      }
      // if there are no other uses, free the register assigned to this
      // interval
      else {
        set.PushRegister(interval->reg);
      }
    }
  };

  expire_set(int_registers_);
  expire_set(float_registers_);
  expire_set(vector_registers_);
}

// If the first argument isn't used after this instruction, its register
// can be reused to take advantage of many architectures supporting
// operations where the destination is the first argument.
// TODO could reorder arguments for communicative binary ops and do this
// with the second argument as well
int RegisterAllocationPass::ReuseArgRegister(IRBuilder &builder, Instr *instr) {
  if (!instr->arg0() || instr->arg0()->constant()) {
    return NO_REGISTER;
  }

  int prefered = instr->arg0()->reg();
  if (prefered == NO_REGISTER) {
    return NO_REGISTER;
  }

  // make sure the register can hold the result type
  const Register &r = registers_[prefered];
  if (!RegisterCanStore(r, instr->type())) {
    return NO_REGISTER;
  }

  // if the argument's register is used after this instruction, it's not
  // trivial to reuse
  Interval *interval = &intervals_[prefered];
  if (interval->next->next()) {
    return NO_REGISTER;
  }

  // the argument's register is not used after the current instruction, so the
  // register can be reused for the result. note, since the interval min/max
  // heap does not support removal of an arbitrary interval, the interval
  // removal must be deferred. since there are no more references, the interval
  // will expire on the next call to ExpireOldIntervals, and then immediately
  // requeued by setting the reused property
  interval->reused = instr;

  return prefered;
}

int RegisterAllocationPass::AllocFreeRegister(Instr *instr) {
  RegisterSet &set = GetRegisterSet(instr->type());

  // get the first free register for this value type
  int reg = set.PopRegister();
  if (reg == NO_REGISTER) {
    return NO_REGISTER;
  }

  // add interval
  Interval *interval = &intervals_[reg];
  interval->instr = instr;
  interval->reused = nullptr;
  interval->start = instr->uses().head();
  interval->end = instr->uses().tail();
  interval->next = interval->start;
  interval->reg = reg;
  set.InsertInterval(interval);

  return reg;
}

int RegisterAllocationPass::AllocBlockedRegister(IRBuilder &builder,
                                                 Instr *instr) {
  InsertPoint insert_point = builder.GetInsertPoint();
  RegisterSet &set = GetRegisterSet(instr->type());

  // spill the register who's next use is furthest away from start
  Interval *interval = set.TailInterval();
  set.PopTailInterval();

  // the interval's value needs to be filled back from from the stack before
  // its next use
  Use *next_ref = interval->next;
  Use *prev_ref = next_ref->prev();
  CHECK(next_ref,
        "Register being spilled has no next use, why wasn't it expired?");

  // allocate a place on the stack to spill the value
  Local *local = builder.AllocLocal(interval->instr->type());

  // insert load before next use
  builder.SetInsertPoint({next_ref->instr()->prev()});
  Instr *load_instr = builder.LoadLocal(local);

  // assign the load a valid ordinal
  int load_ordinal = GetOrdinal(load_instr->prev()) + 1;
  CHECK_LT(load_ordinal, GetOrdinal(load_instr->next()));
  SetOrdinal(load_instr, load_ordinal);

  // update references to interval->instr after the next use to use the new
  // value filled from the stack. this code asssumes that the refs were
  // previously sorted inside of Run().
  while (next_ref) {
    // cache off next next since calling set_value will modify the linked list
    // pointers
    Use *next_next_ref = next_ref->next();
    next_ref->set_value(load_instr);
    next_ref = next_next_ref;
  }

  // insert spill after prev use, note that order here is extremely important.
  // interval->instr's ref list has already been sorted, and when the save
  // instruction is created and added as a reference, the sorted order will be
  // invalidated. because of this, the save instruction needs to be added after
  // the load instruction has updated the sorted references.
  Instr *after = nullptr;

  if (prev_ref) {
    // there is a previous reference, insert store after it
    CHECK(prev_ref->next() == nullptr,
          "All future references should have been replaced");
    after = prev_ref->instr();
  } else {
    // there is no previous reference, insert store immediately after definition
    CHECK(interval->instr->uses().head() == nullptr,
          "All future references should have been replaced");
    after = interval->instr;
  }

  builder.SetInsertPoint({after});
  builder.StoreLocal(local, interval->instr);

  // since the interval that this store belongs to has now expired, there's no
  // need to assign an ordinal to it

  // reuse the old interval
  interval->instr = instr;
  interval->reused = nullptr;
  interval->start = instr->uses().head();
  interval->end = instr->uses().tail();
  interval->next = interval->start;
  set.InsertInterval(interval);

  // reset insert point
  builder.SetInsertPoint(insert_point);

  return interval->reg;
}
