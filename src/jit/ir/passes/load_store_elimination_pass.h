#ifndef LOAD_STORE_ELIMINATION_PASS_H
#define LOAD_STORE_ELIMINATION_PASS_H

#include "jit/ir/passes/pass_runner.h"

namespace re {
namespace jit {
namespace ir {
namespace passes {

struct AvailableEntry {
  int offset;
  Value *value;
};

class LoadStoreEliminationPass : public Pass {
 public:
  static constexpr const char *NAME = "lse";

  LoadStoreEliminationPass();

  const char *name() { return NAME; }

  void Run(IRBuilder &builder);

 private:
  void Reset();

  void Reserve(int offset);
  void ClearAvailable();
  void EraseAvailable(int offset, int size);
  Value *GetAvailable(int offset);
  void SetAvailable(int offset, Value *v);

  AvailableEntry *available_;
  int num_available_;
};
}
}
}
}

#endif
