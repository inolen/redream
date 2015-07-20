#ifndef VALIDATE_INSTRUCTION_PASS_H
#define VALIDATE_INSTRUCTION_PASS_H

#include "cpu/ir/passes/pass_runner.h"

namespace dreavm {
namespace cpu {
namespace ir {
namespace passes {

class ValidateInstructionPass : public Pass {
 public:
  void Run(IRBuilder &builder);

 private:
  void ValidateInstr(Instr *instr);
};
}
}
}
}

#endif
