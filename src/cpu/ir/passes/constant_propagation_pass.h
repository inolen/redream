#ifndef CONSTANT_PROPAGATION_PASS_H
#define CONSTANT_PROPAGATION_PASS_H

#include "cpu/ir/passes/pass_runner.h"

namespace dreavm {
namespace cpu {
namespace ir {
namespace passes {

class ConstantPropagationPass : public Pass {
 public:
  void Run(IRBuilder &builder);

 private:
};
}
}
}
}

#endif
