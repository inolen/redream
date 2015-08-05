#include "core/core.h"
#include "cpu/ir/passes/register_allocation_pass.h"

using namespace dreavm::cpu::backend;
using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;

RegisterAllocationPass::RegisterAllocationPass(
    const backend::Backend &backend) {
  registers_ = backend.registers();
  num_registers_ = backend.num_registers();

  free_ = new int[num_registers_];
  live_ = new std::multiset<Interval>::iterator[num_registers_];
}

RegisterAllocationPass::~RegisterAllocationPass() {
  delete[] live_;
  delete[] free_;
}

void RegisterAllocationPass::Run(IRBuilder &builder) {
  Reset();

  // iterate instructions in reverse postorder, assigning each an ordinal
  AssignOrdinals(builder);

  // iterate blocks in reverse postorder
  Block *block = builder.blocks().head();

  while (block) {
    for (auto instr : block->instrs()) {
      Value *result = instr->result();

      // only allocate registers for results, assume constants can always be
      // encoded as immediates or that the backend has registers reserved
      // for storing the constants
      if (!result) {
        continue;
      }

      // get the live range of the value
      Instr *start = nullptr;
      Instr *end = nullptr;
      GetLiveRange(result, &start, &end);

      // expire any old intervals, freeing up the registers they claimed
      ExpireOldIntervals(start);

      // first, try and reuse the register of one of the incoming arguments
      int reg = ReuuseArgRegister(instr, start, end);
      if (reg == NO_REGISTER) {
        // else, allocate a new register for the result
        reg = AllocFreeRegister(result, start, end);
        if (reg == NO_REGISTER) {
          // if a register couldn't be allocated, spill a register and try again
          reg = AllocBlockedRegister(builder, result, start, end);
          CHECK_NE(reg, NO_REGISTER);
        }
      }

      result->set_reg(reg);
    }

    block = block->rpo_next();
  }
}

void RegisterAllocationPass::Reset() {
  for (num_free_ = 0; num_free_ < num_registers_; num_free_++) {
    free_[num_free_] = num_free_;
  }

  intervals_.clear();
}

void RegisterAllocationPass::AssignOrdinals(IRBuilder &builder) {
  int ordinal = 0;
  Block *block = builder.blocks().head();

  while (block) {
    for (auto instr : block->instrs()) {
      SetOrdinal(instr, ordinal++);
    }

    block = block->rpo_next();
  }
}

void RegisterAllocationPass::GetLiveRange(Value *v, Instr **start,
                                          Instr **end) {
  *start = *end = v->refs().head()->instr();

  for (auto ref : v->refs()) {
    Instr *i = ref->instr();
    if (GetOrdinal(i) < GetOrdinal(*start)) {
      *start = i;
    }
    if (GetOrdinal(i) > GetOrdinal(*end)) {
      *end = i;
    }
  }
}

void RegisterAllocationPass::ExpireOldIntervals(Instr *start) {
  while (intervals_.size()) {
    auto it = intervals_.begin();

    if (GetOrdinal(it->end) >= GetOrdinal(start)) {
      break;
    }

    // move register to free queue
    free_[num_free_++] = it->reg;

    // remove interval
    intervals_.erase(it);
  }
}

void RegisterAllocationPass::UpdateInterval(
    const std::multiset<Interval>::iterator &it, Value *value, Instr *start,
    Instr *end) {
  int reg = it->reg;

  // remove the old interval
  intervals_.erase(it);

  // add the new interval, reusing the previous register
  Interval interval;
  interval.value = value;
  interval.start = start;
  interval.end = end;
  interval.reg = reg;

  // update map with new iterator
  live_[reg] = intervals_.insert(interval);
}

// If the first argument isn't used after this instruction, its register
// can be reused to take advantage of many architectures supporting
// operations where the destination is the first argument.
// TODO could reorder arguments for communicative binary ops and do this
// with the second argument as well
int RegisterAllocationPass::ReuuseArgRegister(Instr *instr, Instr *start,
                                              Instr *end) {
  if (!instr->arg0() || instr->arg0()->constant()) {
    return NO_REGISTER;
  }

  int last_reg = instr->arg0()->reg();
  if (last_reg == NO_REGISTER) {
    return NO_REGISTER;
  }

  // make sure the register can hold the result type
  const Register &r = registers_[last_reg];
  if (!(r.value_types & 1 << (instr->result()->type()))) {
    return NO_REGISTER;
  }

  // if the argument's register is used after this instruction, it can't be
  // reused
  const std::multiset<Interval>::iterator &it = live_[last_reg];
  if (GetOrdinal(it->end) > GetOrdinal(start)) {
    return NO_REGISTER;
  }

  // the argument's register isn't used afterwards, update its interval and
  // reuse
  UpdateInterval(it, instr->result(), start, end);

  return last_reg;
}

int RegisterAllocationPass::AllocFreeRegister(Value *value, Instr *start,
                                              Instr *end) {
  // find the first free register that can store this value type
  // TODO split up free queue into int / float to avoid this scan
  int i;
  for (i = 0; i < num_free_; i++) {
    const Register &r = registers_[free_[i]];
    if (r.value_types & 1 << (value->type())) {
      break;
    }
  }
  if (i == num_free_) {
    return NO_REGISTER;
  }

  // remove register from free queue
  int reg = free_[i];
  free_[i] = free_[--num_free_];

  // add interval
  Interval interval;
  interval.value = value;
  interval.start = start;
  interval.end = end;
  interval.reg = reg;
  auto it = intervals_.insert(interval);

  // add iterator to map so it can be lookedup in the case
  live_[reg] = it;

  return reg;
}

int RegisterAllocationPass::AllocBlockedRegister(IRBuilder &builder,
                                                 Value *value, Instr *start,
                                                 Instr *end) {
  // TODO no longer valid due to type masks
  // CHECK_EQ(num_free_, 0);
  // CHECK_EQ(num_registers_, (int)intervals_.size());

  // spill the register that ends furthest away that can store this type
  auto it = intervals_.rbegin();
  auto e = intervals_.rend();
  for (; it != e; ++it) {
    const Register &r = registers_[it->reg];

    if (r.value_types & 1 << (value->type())) {
      break;
    }
  }
  CHECK(it != e);

  const Interval &to_spill = *it;

  // point spilled value to use stack
  to_spill.value->set_reg(NO_REGISTER);
  to_spill.value->set_local(builder.AllocLocal(to_spill.value->type()));

  // remove interval
  free_[num_free_++] = to_spill.reg;
  intervals_.erase(--it.base());

  return AllocFreeRegister(value, start, end);
}
