#ifndef REGISTER_ALLOCATION_PASS_H
#define REGISTER_ALLOCATION_PASS_H

#include <set>
#include "cpu/backend/backend.h"
#include "cpu/ir/passes/pass_runner.h"

namespace dreavm {
namespace cpu {
namespace ir {
namespace passes {

static inline int GetOrdinal(Instr *i) { return (int)i->tag(); }

static inline void SetOrdinal(Instr *i, int ordinal) {
  i->set_tag((intptr_t)ordinal);
}

struct Interval {
  Value *value;
  Instr *start, *end;
  int reg;

  bool operator<(const Interval &rhs) const {
    return GetOrdinal(end) < GetOrdinal(rhs.end);
  }
};

class RegisterAllocationPass : public Pass {
 public:
  RegisterAllocationPass(const backend::Backend &backend);
  ~RegisterAllocationPass();

  void Run(IRBuilder &builder);

 private:
  const backend::Register *registers_;
  int num_registers_;

  // free register queue
  int *free_, num_free_;

  // interval map, keyed by register
  std::multiset<Interval>::iterator *live_;

  // intervals, sorted in order of increasing end point
  std::multiset<Interval> intervals_;

  void Reset();
  void AssignOrdinals(IRBuilder &builder);
  void GetLiveRange(Value *v, Instr **start, Instr **end);
  void ExpireOldIntervals(Instr *start);
  void UpdateInterval(const std::multiset<Interval>::iterator &it, Value *value,
                      Instr *start, Instr *end);
  int ReuuseArgRegister(Instr *instr, Instr *start, Instr *end);
  int AllocFreeRegister(Value *value, Instr *start, Instr *end);
  int AllocBlockedRegister(IRBuilder &builder, Value *value, Instr *start,
                           Instr *end);
};
}
}
}
}

#endif
