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

Runtime::Runtime(Memory &memory, frontend::Frontend &frontend,
                 backend::Backend &backend)
    : memory_(memory),
      frontend_(frontend),
      backend_(backend),
      lazy_block_(&Runtime::LazyCompile, 0) {
  // set access handler for virtual address space
  memory_.set_virtual_handler(&Runtime::HandleAccessFault, this);

  // setup optimization passes
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidatePass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ControlFlowAnalysisPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new LoadStoreEliminationPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ConstantPropagationPass()));
  pass_runner_.AddPass(
      std::unique_ptr<Pass>(new RegisterAllocationPass(backend_)));

  // initialize all entries in block cache to reference the lazy block
  blocks_ = new RuntimeBlock *[MAX_BLOCKS];
  std::fill_n(blocks_, MAX_BLOCKS, &lazy_block_);
}

Runtime::~Runtime() {
  for (int i = 0; i < MAX_BLOCKS; i++) {
    if (blocks_[i] == &lazy_block_) {
      continue;
    }
    backend_.FreeBlock(blocks_[i]);
  }

  delete[] blocks_;
}

void Runtime::ResetBlocks() {
  // reset block cache
  for (int i = 0; i < MAX_BLOCKS; i++) {
    if (blocks_[i] == &lazy_block_) {
      continue;
    }
    backend_.FreeBlock(blocks_[i]);
    blocks_[i] = &lazy_block_;
  }

  // have the backend reset any underlying data the blocks may have relied on
  backend_.Reset();
}

void Runtime::HandleAccessFault(void *ctx, uintptr_t rip,
                                uintptr_t fault_addr) {
  Runtime *runtime = reinterpret_cast<Runtime *>(ctx);
  runtime->backend_.HandleAccessFault(rip, fault_addr);
}

uint32_t Runtime::LazyCompile(Memory *memory, void *guest_ctx, Runtime *runtime,
                              RuntimeBlock *block, uint32_t addr) {
  RuntimeBlock *new_block = runtime->CompileBlock(addr, guest_ctx);
  runtime->blocks_[BLOCK_OFFSET(addr)] = new_block;
  return new_block->call(memory, guest_ctx, runtime, new_block, addr);
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
