#ifndef CONTEXT_PROMOTION_PASS_H
#define CONTEXT_PROMOTION_PASS_H

#include <vector>
#include "cpu/ir/passes/pass_runner.h"

namespace dreavm {
namespace cpu {
namespace ir {
namespace passes {

class ContextPromotionPass : public Pass {
 public:
  void Run(IRBuilder &builder);

 private:
  void ResetState();
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
