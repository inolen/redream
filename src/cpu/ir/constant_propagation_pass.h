#ifndef CONSTANT_PROPAGATION_PASS_H
#define CONSTANT_PROPAGATION_PASS_H

#include "cpu/ir/pass_runner.h"
#include "emu/memory.h"

namespace dreavm {
namespace cpu {
namespace ir {

class ConstantPropagationPass : public Pass {
 public:
  ConstantPropagationPass(emu::Memory &memory);

  void Run(IRBuilder &builder);

 private:
  emu::Memory &memory_;
};
}
}
}

#endif
