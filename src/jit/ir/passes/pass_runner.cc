#include "core/core.h"
#include "emu/profiler.h"
#include "jit/ir/ir_builder.h"
#include "jit/ir/passes/pass_runner.h"

using namespace dvm::jit::ir;
using namespace dvm::jit::ir::passes;

PassRunner::PassRunner() {}

void PassRunner::AddPass(std::unique_ptr<Pass> pass) {
  passes_.push_back(std::move(pass));
}

void PassRunner::Run(IRBuilder &builder) {
  PROFILER_RUNTIME("PassRunner::Run");

  for (auto &pass : passes_) {
    pass->Run(builder);
  }
}
