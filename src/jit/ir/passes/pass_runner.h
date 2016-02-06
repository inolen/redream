#ifndef PASS_RUNNER_H
#define PASS_RUNNER_H

#include <memory>
#include <vector>
#include "jit/ir/ir_builder.h"

namespace re {
namespace jit {
namespace ir {
namespace passes {

class Pass {
 public:
  virtual ~Pass() {}
  virtual void Run(IRBuilder &builder) = 0;
};

class PassRunner {
 public:
  PassRunner();

  void AddPass(std::unique_ptr<Pass> pass);
  void Run(IRBuilder &builder);

 private:
  std::vector<std::unique_ptr<Pass>> passes_;
};
}
}
}
}

#endif
