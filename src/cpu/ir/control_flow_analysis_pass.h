#ifndef CONTROL_FLOW_ANALYSIS_PASS_H
#define CONTROL_FLOW_ANALYSIS_PASS_H

#include "cpu/ir/pass_runner.h"

namespace dreavm {
namespace cpu {
namespace ir {

class ControlFlowAnalysisPass : public Pass {
 public:
  void Run(IRBuilder &builder);
};
}
}
}

#endif
