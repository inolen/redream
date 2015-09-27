#include "emu/profiler.h"
#include "jit/backend/backend.h"
#include "jit/frontend/frontend.h"
#include "jit/ir/ir_builder.h"
#include "jit/ir/passes/constant_propagation_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/control_flow_analysis_pass.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "jit/ir/passes/validate_pass.h"
#include "jit/runtime.h"

using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend;
using namespace dreavm::jit::ir;
using namespace dreavm::jit::ir::passes;

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
    : memory_(memory), frontend_(frontend), backend_(backend) {
  blocks_ = new RuntimeBlock* [MAX_BLOCKS]();

  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidatePass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ControlFlowAnalysisPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new LoadStoreEliminationPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ConstantPropagationPass()));
  pass_runner_.AddPass(
      std::unique_ptr<Pass>(new RegisterAllocationPass(backend_)));
}

Runtime::~Runtime() {
  ResetBlocks();
  delete[] blocks_;
}

// TODO should the block caching be part of the frontend?
// this way, the SH4Frontend can cache based on FPU state
RuntimeBlock *Runtime::GetBlock(uint32_t addr, const void *guest_ctx) {
  uint32_t offset = BlockOffset(addr);
  if (offset >= MAX_BLOCKS) {
    LOG_FATAL("Block requested at 0x%x is outside of the executable space",
              addr);
  }

  RuntimeBlock *block = blocks_[offset];
  if (block) {
    return block;
  }

  return (blocks_[offset] = CompileBlock(addr, guest_ctx));
}

void Runtime::ResetBlocks() {
  // reset our local block cache
  for (int i = 0; i < MAX_BLOCKS; i++) {
    if (blocks_[i]) {
      backend_.FreeBlock(blocks_[i]);
      blocks_[i] = nullptr;
    }
  }

  // have the backend reset any data the blocks may have relied on
  backend_.Reset();
}

RuntimeBlock *Runtime::CompileBlock(uint32_t addr, const void *guest_ctx) {
  PROFILER_RUNTIME("Runtime::CompileBlock");

  std::unique_ptr<IRBuilder> builder = frontend_.BuildBlock(addr, guest_ctx);

  // run optimization passes
  pass_runner_.Run(*builder);

  // try to assemble the block
  RuntimeBlock *block = backend_.AssembleBlock(*builder);

  if (!block) {
    LOG_INFO("Assembler overflow, resetting block cache");

    // the backend overflowed, reset the block cache
    ResetBlocks();

    // if the backend fails to assemble on an empty cache, there's nothing to be
    // done
    block = backend_.AssembleBlock(*builder);

    CHECK(block, "Backend assembler buffer overflow");
  }

  return block;
}
