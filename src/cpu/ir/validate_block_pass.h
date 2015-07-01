#ifndef VALIDATE_BLOCK_PASS_H
#define VALIDATE_BLOCK_PASS_H

#include "cpu/ir/pass_runner.h"

namespace dreavm {
namespace cpu {
namespace ir {

class ValidateBlockPass : public Pass {
 public:
  void Run(IRBuilder &builder);
};
}
}
}

#endif
