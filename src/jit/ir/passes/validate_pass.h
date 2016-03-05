#ifndef VALIDATE_BLOCK_PASS_H
#define VALIDATE_BLOCK_PASS_H

#include "jit/ir/passes/pass_runner.h"

namespace re {
namespace jit {
namespace ir {
namespace passes {

class ValidatePass : public Pass {
 public:
  void Run(IRBuilder &builder);
};
}
}
}
}

#endif
