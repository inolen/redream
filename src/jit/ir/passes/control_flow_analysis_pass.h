#ifndef CONTROL_FLOW_ANALYSIS_PASS_H
#define CONTROL_FLOW_ANALYSIS_PASS_H

#include "jit/ir/passes/pass_runner.h"

namespace dreavm {
namespace jit {
namespace ir {
namespace passes {

class ControlFlowAnalysisPass : public Pass {
 public:
  void Run(IRBuilder &builder);
};
}
}
}
}

#endif
