#ifndef REGISTER_ALLOCATION_PASS_H
#define REGISTER_ALLOCATION_PASS_H

#include <list>
#include "cpu/backend/backend.h"
#include "cpu/ir/passes/pass_runner.h"

namespace dreavm {
namespace cpu {
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
  void PopHeadInterval();
  Interval *TailInterval();
  void PopTailInterval();
  void InsertInterval(Interval *interval);

 private:
  int max_registers_;

  // free register queues
  int *free;
  int num_free;

  // intervals used by this register set, sorted in order of next use
  Interval **live_;
  int live_head_;
  int num_live_;
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

  void ResetState();
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
