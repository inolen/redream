#ifndef VALIDATE_BLOCK_PASS_H
#define VALIDATE_BLOCK_PASS_H

#include "jit/ir/passes/pass_runner.h"

namespace dvm {
namespace jit {
namespace ir {
namespace passes {

class ValidatePass : public Pass {
 public:
  void Run(IRBuilder &builder);
  void ValidateBlock(IRBuilder &builder, Block *block);
  void ValidateInstr(IRBuilder &builder, Block *block, Instr *instr);
};
}
}
}
}

#endif
