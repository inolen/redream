#include "cpu/backend/backend.h"
#include "cpu/frontend/frontend.h"
#include "cpu/ir/ir_builder.h"
#include "cpu/ir/passes/constant_propagation_pass.h"
#include "cpu/ir/passes/context_promotion_pass.h"
#include "cpu/ir/passes/control_flow_analysis_pass.h"
#include "cpu/ir/passes/register_allocation_pass.h"
#include "cpu/ir/passes/validate_pass.h"
#include "cpu/runtime.h"
#include "emu/profiler.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::backend;
using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;
using namespace dreavm::emu;

// executable code sits between 0x0c000000 and 0x0d000000 (16mb). each instr
// is 2 bytes, making for a maximum of 0x1000000 >> 1 blocks
enum {
  BLOCK_ADDR_SHIFT = 1,
  BLOCK_ADDR_MASK = ~0xfc000000,
  MAX_BLOCKS = 0x1000000 >> BLOCK_ADDR_SHIFT,
};

static inline uint32_t BlockOffset(uint32_t addr) {
  return (addr & BLOCK_ADDR_MASK) >> BLOCK_ADDR_SHIFT;
}

Runtime::Runtime(Memory &memory, frontend::Frontend &frontend,
                 backend::Backend &backend)
    : memory_(memory),
      frontend_(frontend),
      backend_(backend),
      pending_reset_(false) {
  blocks_ = new RuntimeBlock[MAX_BLOCKS];

  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidatePass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ControlFlowAnalysisPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ContextPromotionPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ConstantPropagationPass()));
  pass_runner_.AddPass(
      std::unique_ptr<Pass>(new RegisterAllocationPass(backend_)));
}

Runtime::~Runtime() { delete[] blocks_; }

// TODO should the block caching be part of the frontend?
// this way, the SH4Frontend can cache based on FPU state
RuntimeBlock *Runtime::GetBlock(uint32_t addr, const void *guest_ctx) {
  if (pending_reset_) {
    ResetBlocks();
    pending_reset_ = false;
  }

  uint32_t offset = BlockOffset(addr);
  if (offset >= MAX_BLOCKS) {
    LOG_FATAL("Block requested at 0x%x is outside of the executable space",
              addr);
  }

  RuntimeBlock *block = &blocks_[offset];

  if (!block->call) {
    CompileBlock(addr, guest_ctx, block);
    CHECK(block->call);
  }

  return block;
}

void Runtime::QueueResetBlocks() { pending_reset_ = true; }

void Runtime::ResetBlocks() {
  backend_.Reset();

  memset(blocks_, 0, sizeof(RuntimeBlock) * MAX_BLOCKS);
}

void Runtime::CompileBlock(uint32_t addr, const void *guest_ctx,
                           RuntimeBlock *block) {
  PROFILER_RUNTIME("Runtime::CompileBlock");

  std::unique_ptr<IRBuilder> builder = frontend_.BuildBlock(addr, guest_ctx);

  // run optimization passes
  pass_runner_.Run(*builder);

  if (!backend_.AssembleBlock(*builder, block)) {
    LOG_INFO("Assembler overflow, resetting block cache");

    // the backend overflowed, reset the block cache
    ResetBlocks();

    // if the backend fails to assemble on an empty cache, there's nothing to be
    // done
    CHECK(backend_.AssembleBlock(*builder, block),
          "Backend assembler buffer overflow");
  }
}
