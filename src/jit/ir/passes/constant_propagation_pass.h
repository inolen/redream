#ifndef CONSTANT_PROPAGATION_PASS_H
#define CONSTANT_PROPAGATION_PASS_H

#include "jit/ir/passes/pass_runner.h"

namespace re {
namespace jit {
namespace ir {
namespace passes {

class ConstantPropagationPass : public Pass {
 public:
  static const char *NAME = "constprop";

  const char *name() { return NAME; }

  void Run(IRBuilder &builder);
};
}
}
}
}

#endif
