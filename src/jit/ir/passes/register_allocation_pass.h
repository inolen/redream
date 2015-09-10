#ifndef REGISTER_ALLOCATION_PASS_H
#define REGISTER_ALLOCATION_PASS_H

#include "core/ring_buffer.h"
#include "jit/backend/backend.h"
#include "jit/ir/passes/pass_runner.h"

namespace dreavm {
namespace jit {
namespace ir {
namespace passes {

struct Interval {
  Value *value;
  ValueRef *start;
  ValueRef *next;
  ValueRef *end;
  int reg;
};

class RegisterSet {
 public:
  RegisterSet(int max_registers);
  ~RegisterSet();

  void Clear();

  int PopRegister();
  void PushRegister(int reg);

  Interval *HeadInterval();
  Interval *TailInterval();
  void PopHeadInterval();
  void PopTailInterval();
  void InsertInterval(Interval *interval);

 private:
  // free register vector
  int *free_, num_free_;

  // intervals used by this register set, sorted in order of next use
  core::RingBuffer<Interval *> live_;
};

class RegisterAllocationPass : public Pass {
 public:
  RegisterAllocationPass(const backend::Backend &backend);
  ~RegisterAllocationPass();

  void Run(IRBuilder &builder);

 private:
  const backend::Register *registers_;
  int num_registers_;

  RegisterSet int_registers_;
  RegisterSet float_registers_;

  // intervals, keyed by register
  Interval *intervals_;

  RegisterSet &GetRegisterSet(ValueTy type);

  void Reset();
  void AssignOrdinals(ir::Block *block);
  void ExpireOldIntervals(Instr *start);
  int ReuuseArgRegister(Instr *instr, ValueRef *start, ValueRef *end);
  int AllocFreeRegister(Value *value, ValueRef *start, ValueRef *end);
  int AllocBlockedRegister(IRBuilder &builder, Value *value, ValueRef *start,
                           ValueRef *end);
};
}
}
}
}

#endif
