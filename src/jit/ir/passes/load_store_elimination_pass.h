#ifndef LOAD_STORE_ELIMINATION_PASS_H
#define LOAD_STORE_ELIMINATION_PASS_H

#include <vector>
#include "jit/ir/passes/pass_runner.h"

namespace dreavm {
namespace jit {
namespace ir {
namespace passes {

class LoadStoreEliminationPass : public Pass {
 public:
  void Run(IRBuilder &builder);

 private:
  void Reset();
  void ProcessBlock(Block *block);
  void ClearAvailable();
  void ReserveAvailable(int offset);
  Value *GetAvailable(int offset);
  void SetAvailable(int offset, Value *v);

  uint64_t available_marker_;
  std::vector<uint64_t> available_;
  std::vector<Value *> available_values_;
};
}
}
}
}

#endif
