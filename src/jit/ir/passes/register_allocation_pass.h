#ifndef REGISTER_ALLOCATION_PASS_H
#define REGISTER_ALLOCATION_PASS_H

#include <vector>
#include "jit/backend/backend.h"
#include "jit/ir/passes/pass_runner.h"

namespace re {
namespace jit {
namespace ir {
namespace passes {

struct Interval {
  Instr *instr;
  Instr *reused;
  Use *start;
  Use *end;
  Use *next;
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
  Interval **live_;
  int num_live_;
};

class RegisterAllocationPass : public Pass {
 public:
  RegisterAllocationPass(const backend::Backend &backend);
  ~RegisterAllocationPass();

  const char *name() { return "ra"; }

  void Run(IRBuilder &builder);

 private:
  const backend::Register *registers_;
  int num_registers_;

  RegisterSet int_registers_;
  RegisterSet float_registers_;
  RegisterSet vector_registers_;

  // intervals, keyed by register
  Interval *intervals_;

  RegisterSet &GetRegisterSet(ValueType type);

  void Reset();
  void AssignOrdinals(IRBuilder &builder);
  void ExpireOldIntervals(Instr *instr);
  int ReuseArgRegister(IRBuilder &builder, Instr *instr);
  int AllocFreeRegister(Instr *instr);
  int AllocBlockedRegister(IRBuilder &builder, Instr *instr);
};
}
}
}
}

#endif
