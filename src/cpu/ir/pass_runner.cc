#include "core/core.h"
#include "cpu/ir/ir_builder.h"
#include "cpu/ir/pass_runner.h"

using namespace dreavm;
using namespace dreavm::cpu::ir;

PassRunner::PassRunner() {}

void PassRunner::AddPass(std::unique_ptr<Pass> pass) {
  passes_.push_back(std::move(pass));
}

void PassRunner::Run(IRBuilder &builder) {
  for (auto &pass : passes_) {
    pass->Run(builder);
  }
}
