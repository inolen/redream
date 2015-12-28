#include "emu/profiler.h"
#include "jit/backend/backend.h"
#include "jit/frontend/frontend.h"
#include "jit/ir/ir_builder.h"
#include "jit/ir/passes/constant_propagation_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "jit/ir/passes/validate_pass.h"
#include "jit/runtime.h"

using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend;
using namespace dreavm::jit::ir;
using namespace dreavm::jit::ir::passes;
using namespace dreavm::sys;

Runtime::Runtime(Memory &memory, frontend::Frontend &frontend,
                 backend::Backend &backend, BlockRunner default_handler)
    : memory_(memory),
      frontend_(frontend),
      backend_(backend),
      compile_block_(default_handler) {
  eh_handle_ =
      ExceptionHandler::instance().AddHandler(this, &Runtime::HandleException);

  // setup optimization passes
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidatePass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new LoadStoreEliminationPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ConstantPropagationPass()));
  pass_runner_.AddPass(
      std::unique_ptr<Pass>(new RegisterAllocationPass(backend_)));

  // initialize all entries in block cache to reference the compile block
  blocks_ = new BlockRunner[MAX_BLOCKS];
  std::fill_n(blocks_, MAX_BLOCKS, compile_block_);
}

Runtime::~Runtime() {
  delete[] blocks_;

  ExceptionHandler::instance().RemoveHandler(eh_handle_);
}

BlockRunner Runtime::CompileBlock(uint32_t addr, void *guest_ctx) {
  PROFILER_RUNTIME("Runtime::CompileBlock");

  std::unique_ptr<IRBuilder> builder = frontend_.BuildBlock(addr, guest_ctx);

  // run optimization passes
  pass_runner_.Run(*builder);

  // try to assemble the block
  BlockRunner block = backend_.AssembleBlock(*builder, guest_ctx);

  if (!block) {
    LOG_INFO("Assembler overflow, resetting block cache");

    // the backend overflowed, reset the block cache
    ResetBlocks();

    // if the backend fails to assemble on an empty cache, there's nothing to be
    // done
    block = backend_.AssembleBlock(*builder, guest_ctx);

    CHECK(block, "Backend assembler buffer overflow");
  }

  // add the block to the cache
  blocks_[BLOCK_OFFSET(addr)] = block;

  return block;
}

void Runtime::ResetBlocks() {
  // reset block cache
  std::fill_n(blocks_, MAX_BLOCKS, compile_block_);

  // have the backend reset any underlying data the blocks may have relied on
  backend_.Reset();
}

bool Runtime::HandleException(void *ctx, Exception &ex) {
  Runtime *runtime = reinterpret_cast<Runtime *>(ctx);
  const uint8_t *fault_addr = reinterpret_cast<const uint8_t *>(ex.fault_addr);
  const uint8_t *protected_start = runtime->memory_.protected_base();
  const uint8_t *protected_end =
      protected_start + runtime->memory_.total_size();

  if (fault_addr < protected_start || fault_addr >= protected_end) {
    return false;
  }

  return runtime->backend_.HandleException(ex);
}
