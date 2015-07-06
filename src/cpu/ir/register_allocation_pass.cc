#include "core/core.h"
#include "cpu/ir/register_allocation_pass.h"

using namespace dreavm;
using namespace dreavm::cpu::ir;

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
      // encoded by immediates or that the backend has registers reserved
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

      // if the last argument isn't used after this instruction, its register
      // can be reused to take advantage of many architectures supporting
      // operations where the destination is the last source argument
      // FIXME could reorder arguments and do this with any source arguments
      // meeting the criteria
      Value *last_arg = instr->arg2()
                            ? instr->arg2()
                            : (instr->arg1() ? instr->arg1() : instr->arg0());
      if (last_arg && !last_arg->constant()) {
        // get the current interval for this register
        int last_reg = last_arg->reg();

        if (last_reg != NO_REGISTER) {
          const std::multiset<Interval>::iterator &it = live_[last_reg];

          // if the argument isn't used after this instruction, reuse its
          // register for the result
          if (GetOrdinal(it->end) <= GetOrdinal(start)) {
            UpdateInterval(it, result, start, end);
            result->set_reg(last_reg);
            continue;
          }
        }
      }

      // else, allocate a new register
      int reg = AllocFreeRegister(result, start, end);
      if (reg == NO_REGISTER) {
        reg = AllocBlockedRegister(builder, result, start, end);
        CHECK_NE(reg, NO_REGISTER);
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

  // printf("UpdateRegister %d (%p) -> %d (%p) : (%p)\n", GetOrdinal(start),
  // start, GetOrdinal(end), end, value);

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

int RegisterAllocationPass::AllocFreeRegister(Value *value, Instr *start,
                                              Instr *end) {
  if (!num_free_) {
    // LOG(WARNING) << "AllocFreeRegister failed for " << GetOrdinal(start);
    return NO_REGISTER;
  }

  // printf("AllocFreeRegister %d (%p) -> %d (%p) : (%p)\n", GetOrdinal(start),
  // start, GetOrdinal(end), end, value);

  // remove register from free queue
  int reg = free_[0];
  free_[0] = free_[--num_free_];

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
  CHECK_EQ(num_free_, 0);
  CHECK_EQ(num_registers_, (int)intervals_.size());

  // spill the register that ends furthest away, or possibly this register
  // itself
  auto it = --intervals_.end();
  const Interval &to_spill = *it;

  // point spilled value to use stack
  to_spill.value->set_reg(NO_REGISTER);
  to_spill.value->set_local(builder.AllocLocal(to_spill.value->type()));

  // printf("Spilling %d (%p) -> %d (%p) : (%p)\n", GetOrdinal(to_spill.start),
  // to_spill.start, GetOrdinal(to_spill.end), to_spill.end, to_spill.value);

  // remove interval
  free_[num_free_++] = to_spill.reg;
  intervals_.erase(it);

  return AllocFreeRegister(value, start, end);
}
