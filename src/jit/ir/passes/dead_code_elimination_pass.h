#ifndef DEAD_CODE_ELIMINATION_PASS_H
#define DEAD_CODE_ELIMINATION_PASS_H

#include "jit/backend/backend.h"
#include "jit/ir/passes/pass_runner.h"

namespace re {
namespace jit {
namespace ir {
namespace passes {

class DeadCodeEliminationPass : public Pass {
 public:
  const char *name() { return "dce"; }

  void Run(IRBuilder &builder);
};
}
}
}
}

#endif
