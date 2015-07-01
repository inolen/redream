#ifndef CONTEXT_PROMOTION_PASS_H
#define CONTEXT_PROMOTION_PASS_H

#include <vector>
#include "cpu/ir/pass_runner.h"

namespace dreavm {
namespace cpu {
namespace ir {

class ContextPromotionPass : public Pass {
 public:
  void Run(IRBuilder &builder);

 private:
  void ProcessBlock(Block *block);

  void ClearAvailable();
  Value *GetAvailable(int offset);
  void SetAvailable(int offset, Value *v);

  std::vector<Value *> available_;
};
}
}
}

#endif
