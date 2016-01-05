#include "emu/profiler.h"
#include "hw/sh4/sh4_code_cache.h"
#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/ir/ir_builder.h"
#include "jit/ir/passes/constant_propagation_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "jit/ir/passes/validate_pass.h"

using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend;
using namespace dreavm::jit::backend::interpreter;
using namespace dreavm::jit::backend::x64;
using namespace dreavm::jit::frontend;
using namespace dreavm::jit::frontend::sh4;
using namespace dreavm::jit::ir;
using namespace dreavm::jit::ir::passes;
using namespace dreavm::sys;

DEFINE_bool(interpreter, false, "Use interpreter");

SH4CodeCache::SH4CodeCache(Memory &memory, BlockPointer default_block)
    : memory_(memory), default_block_(default_block) {
  // add exception handler to help recompile blocks when protected memory is
  // accessed
  eh_handle_ = ExceptionHandler::instance().AddHandler(
      this, &SH4CodeCache::HandleException);

  // setup parser and emitter
  frontend_ = new SH4Frontend(memory);

  if (FLAGS_interpreter) {
    backend_ = new InterpreterBackend(memory);
  } else {
    backend_ = new X64Backend(memory);
  }

  // setup optimization passes
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidatePass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new LoadStoreEliminationPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ConstantPropagationPass()));
  pass_runner_.AddPass(
      std::unique_ptr<Pass>(new RegisterAllocationPass(*backend_)));

  // initialize all entries in block cache to reference the compile block
  blocks_ = new BlockPointer[MAX_BLOCKS];
  std::fill_n(blocks_, MAX_BLOCKS, default_block_);
}

SH4CodeCache::~SH4CodeCache() {
  ExceptionHandler::instance().RemoveHandler(eh_handle_);

  delete frontend_;
  delete backend_;

  delete[] blocks_;
}

BlockPointer SH4CodeCache::CompileBlock(uint32_t addr, void *guest_ctx) {
  PROFILER_RUNTIME("SH4CodeCache::CompileBlock");

  std::unique_ptr<IRBuilder> builder = frontend_->BuildBlock(addr, guest_ctx);

  // run optimization passes
  pass_runner_.Run(*builder);

  // try to assemble the block
  BlockPointer block = backend_->AssembleBlock(*builder, guest_ctx);

  if (!block) {
    LOG_INFO("Assembler overflow, resetting block cache");

    // the backend overflowed, reset the block cache
    ResetBlocks();

    // if the backend fails to assemble on an empty cache, there's nothing to be
    // done
    block = backend_->AssembleBlock(*builder, guest_ctx);

    CHECK(block, "Backend assembler buffer overflow");
  }

  // add the block to the cache
  blocks_[BLOCK_OFFSET(addr)] = block;

  return block;
}

void SH4CodeCache::ResetBlocks() {
  // reset block cache
  std::fill_n(blocks_, MAX_BLOCKS, default_block_);

  // have the backend reset any underlying data the blocks may have relied on
  backend_->Reset();
}

bool SH4CodeCache::HandleException(void *ctx, Exception &ex) {
  SH4CodeCache *self = reinterpret_cast<SH4CodeCache *>(ctx);
  const uint8_t *fault_addr = reinterpret_cast<const uint8_t *>(ex.fault_addr);
  const uint8_t *protected_start = self->memory_.protected_base();
  const uint8_t *protected_end = protected_start + self->memory_.total_size();

  if (fault_addr < protected_start || fault_addr >= protected_end) {
    return false;
  }

  return self->backend_->HandleException(ex);
}
