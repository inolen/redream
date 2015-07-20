#ifndef CONTROL_FLOW_ANALYSIS_PASS_H
#define CONTROL_FLOW_ANALYSIS_PASS_H

#include "cpu/ir/passes/pass_runner.h"

namespace dreavm {
namespace cpu {
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
