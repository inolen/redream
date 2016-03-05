#include "emu/profiler.h"
#include "jit/ir/passes/validate_pass.h"

using namespace re::jit::ir;
using namespace re::jit::ir::passes;

void ValidatePass::Run(IRBuilder &builder) {
  PROFILER_RUNTIME("ValidatePass::Run");
}
