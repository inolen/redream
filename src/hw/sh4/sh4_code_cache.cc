#include <gflags/gflags.h>
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

using namespace re::hw;
using namespace re::jit;
using namespace re::jit::backend;
using namespace re::jit::backend::interpreter;
using namespace re::jit::backend::x64;
using namespace re::jit::frontend;
using namespace re::jit::frontend::sh4;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;
using namespace re::sys;

DEFINE_bool(interpreter, false, "Use interpreter");

SH4CodeCache::SH4CodeCache(Memory *memory, BlockPointer default_block)
    : default_block_(default_block) {
  // add exception handler to help recompile blocks when protected memory is
  // accessed
  eh_handle_ = ExceptionHandler::instance().AddHandler(
      this, &SH4CodeCache::HandleException);

  // setup parser and emitter
  frontend_ = new SH4Frontend(*memory);

  if (FLAGS_interpreter) {
    backend_ = new InterpreterBackend(*memory);
  } else {
    backend_ = new X64Backend(*memory);
  }

  // setup optimization passes
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidatePass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new LoadStoreEliminationPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ConstantPropagationPass()));
  pass_runner_.AddPass(
      std::unique_ptr<Pass>(new RegisterAllocationPass(*backend_)));

  // initialize all entries in block cache to reference the default block
  blocks_ = new BlockEntry[MAX_BLOCKS]();

  for (int i = 0; i < MAX_BLOCKS; i++) {
    BlockEntry *block = &blocks_[i];
    block->run = default_block_;
  }
}

SH4CodeCache::~SH4CodeCache() {
  ExceptionHandler::instance().RemoveHandler(eh_handle_);

  delete frontend_;
  delete backend_;

  delete[] blocks_;
}

BlockEntry *SH4CodeCache::CompileBlock(uint32_t addr, void *guest_ctx) {
  PROFILER_RUNTIME("SH4CodeCache::CompileBlock");

  BlockEntry *block = &blocks_[BLOCK_OFFSET(addr)];

  std::unique_ptr<IRBuilder> builder = frontend_->BuildBlock(addr, guest_ctx);

  pass_runner_.Run(*builder);

  // try to assemble the block
  BlockPointer run =
      backend_->AssembleBlock(*builder, source_map_, guest_ctx, block->flags);

  if (!run) {
    LOG_INFO("Assembler overflow, resetting block cache");

    // the backend overflowed, reset the block cache
    ResetBlocks();

    // if the backend fails to assemble on an empty cache, there's nothing to be
    // done
    run =
        backend_->AssembleBlock(*builder, source_map_, guest_ctx, block->flags);

    CHECK(run, "Backend assembler buffer overflow");
  }

  // update cache entry
  block->run = run;
  block->flags = 0;

  return block;
}

void SH4CodeCache::ResetBlocks() {
  // reset block cache
  for (int i = 0; i < MAX_BLOCKS; i++) {
    BlockEntry *block = &blocks_[i];
    block->run = default_block_;
    block->flags = 0;
  }

  // have the backend reset any underlying data the blocks may have relied on
  backend_->Reset();

  // reset source map
  source_map_.Reset();
}

bool SH4CodeCache::HandleException(void *ctx, Exception &ex) {
  SH4CodeCache *self = reinterpret_cast<SH4CodeCache *>(ctx);

  // see if there is an assembled block corresponding to the current pc
  uint32_t block_addr = 0;
  if (!self->source_map_.LookupBlockAddress(ex.pc, &block_addr)) {
    return false;
  }

  // let the backend attempt to handle the exception
  BlockEntry *block = self->GetBlock(block_addr);
  if (!self->backend_->HandleException(block->run, &block->flags, ex)) {
    return false;
  }

  // invalidate the block if the backend says so
  if (block->flags & BF_INVALIDATE) {
    block->run = self->default_block_;
    block->flags &= ~BF_INVALIDATE;
  }

  return true;
}
