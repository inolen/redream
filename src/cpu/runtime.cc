#include "cpu/backend/backend.h"
#include "cpu/frontend/frontend.h"
#include "cpu/ir/ir_builder.h"
#include "cpu/ir/passes/constant_propagation_pass.h"
#include "cpu/ir/passes/context_promotion_pass.h"
#include "cpu/ir/passes/control_flow_analysis_pass.h"
#include "cpu/ir/passes/register_allocation_pass.h"
#include "cpu/ir/passes/validate_block_pass.h"
#include "cpu/ir/passes/validate_instruction_pass.h"
#include "cpu/runtime.h"
#include "emu/profiler.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;
using namespace dreavm::emu;

Runtime::Runtime(Memory &memory)
    : memory_(memory),
      frontend_(nullptr),
      backend_(nullptr),
      pending_reset_(false) {
  blocks_ = new std::unique_ptr<RuntimeBlock>[MAX_BLOCKS];
}

Runtime::~Runtime() { delete[] blocks_; }

bool Runtime::Init(frontend::Frontend *frontend, backend::Backend *backend) {
  frontend_ = frontend;
  backend_ = backend;

  if (!frontend_->Init()) {
    return false;
  }

  if (!backend_->Init()) {
    return false;
  }

  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidateBlockPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ControlFlowAnalysisPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ContextPromotionPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ConstantPropagationPass()));
  pass_runner_.AddPass(
      std::unique_ptr<Pass>(new RegisterAllocationPass(*backend_)));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidateInstructionPass()));

  return true;
}

RuntimeBlock *Runtime::ResolveBlock(uint32_t addr) {
  PROFILER_SCOPE_F("runtime");

  if (pending_reset_) {
    for (int i = 0; i < MAX_BLOCKS; i++) {
      blocks_[i] = nullptr;
    }
    pending_reset_ = false;
  }

  addr &= 0x1fffffff;
  uint32_t offset = BlockOffset(addr);
  if (offset >= MAX_BLOCKS) {
    LOG(FATAL) << "Block requested at 0x" << std::hex << addr
               << " is outside of the executable space";
  }

  RuntimeBlock *existing_block = blocks_[offset].get();
  if (existing_block) {
    return existing_block;
  }

  return CompileBlock(addr);
}

void Runtime::ResetBlocks() { pending_reset_ = true; }

RuntimeBlock *Runtime::CompileBlock(uint32_t addr) {
  PROFILER_SCOPE_F("runtime");

  // LOG(INFO) << "Compiling block 0x" << std::hex << addr;

  std::unique_ptr<IRBuilder> builder = frontend_->BuildBlock(addr);
  if (!builder) {
    return nullptr;
  }

  // printf("BEFORE:\n");
  // builder->Dump();

  // run optimization passes
  pass_runner_.Run(*builder);

  // printf("AFTER:\n");
  // builder->Dump();

  std::unique_ptr<RuntimeBlock> block = backend_->AssembleBlock(*builder);
  if (!block) {
    return nullptr;
  }

  uint32_t offset = BlockOffset(addr);

  blocks_[offset] = std::move(block);

  return blocks_[offset].get();
}
