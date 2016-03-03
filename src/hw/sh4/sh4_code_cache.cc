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

SH4CodeCache::SH4CodeCache(Memory *memory, void *guest_ctx,
                           BlockPointer default_block)
    : default_block_(default_block) {
  // add exception handler to help recompile blocks when protected memory is
  // accessed
  eh_handle_ = ExceptionHandler::instance().AddHandler(
      this, &SH4CodeCache::HandleException);

  // setup parser and emitter
  frontend_ = new SH4Frontend(*memory, guest_ctx);

  if (FLAGS_interpreter) {
    backend_ = new InterpreterBackend(*memory, guest_ctx);
  } else {
    backend_ = new X64Backend(*memory, guest_ctx);
  }

  // setup optimization passes
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidatePass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new LoadStoreEliminationPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ConstantPropagationPass()));
  pass_runner_.AddPass(
      std::unique_ptr<Pass>(new RegisterAllocationPass(*backend_)));

  // initialize all entries in block cache to reference the default block
  blocks_ = new SH4BlockEntry[MAX_BLOCKS]();

  for (int i = 0; i < MAX_BLOCKS; i++) {
    SH4BlockEntry *block = &blocks_[i];
    block->run = default_block_;
    block->flags = 0;
    block->it = block_map_.end();
    block->rit = reverse_block_map_.end();
  }
}

SH4CodeCache::~SH4CodeCache() {
  ExceptionHandler::instance().RemoveHandler(eh_handle_);

  delete frontend_;
  delete backend_;

  delete[] blocks_;
}

SH4BlockEntry *SH4CodeCache::CompileBlock(uint32_t addr, int max_instrs) {
  PROFILER_RUNTIME("SH4CodeCache::CompileBlock");

  int offset = BLOCK_OFFSET(addr);
  CHECK_LT(offset, MAX_BLOCKS);
  SH4BlockEntry *block = &blocks_[offset];

  // make sure there wasn't a valid block pointer in the entry
  CHECK_EQ(block->run, default_block_);

  // if the block being compiled had previously been invalidated by a
  // fastmem exception, remove it from the lookup maps at this point
  if (block->it != block_map_.end()) {
    block_map_.erase(block->it);
  }
  if (block->rit != reverse_block_map_.end()) {
    reverse_block_map_.erase(block->rit);
  }

  // compile the SH4 into IR
  std::unique_ptr<IRBuilder> builder = frontend_->BuildBlock(addr, max_instrs);

  pass_runner_.Run(*builder);

  // assemble the IR into native code
  BlockPointer run = backend_->AssembleBlock(*builder, block->flags);

  if (!run) {
    LOG_INFO("Assembler overflow, resetting block cache");

    // the backend overflowed, completely clear the block cache
    ClearBlocks();

    // if the backend fails to assemble on an empty cache, there's nothing to be
    // done
    run = backend_->AssembleBlock(*builder, block->flags);

    CHECK(run, "Backend assembler buffer overflow");
  }

  // add the cache entry to the lookup maps
  auto res = block_map_.insert(std::make_pair(addr, block));
  CHECK(res.second);

  auto rres = reverse_block_map_.insert(
      std::make_pair(reinterpret_cast<uintptr_t>(run), block));
  CHECK(rres.second);

  // update cache entry
  block->run = run;
  block->flags = 0;
  block->it = res.first;
  block->rit = rres.first;

  return block;
}
void SH4CodeCache::RemoveBlocks(uint32_t addr) {
  // remove any block which overlaps the address
  while (true) {
    SH4BlockEntry *block = LookupBlock(addr);

    if (!block) {
      break;
    }

    // erase the cache entry from the lookup maps
    block_map_.erase(block->it);
    reverse_block_map_.erase(block->rit);

    // reset the cache entry
    block->run = default_block_;
    block->flags = 0;
    block->it = block_map_.end();
    block->rit = reverse_block_map_.end();
  }
}

void SH4CodeCache::UnlinkBlocks() {
  // unlink the block pointers, but don't remove the map entries. this is used
  // when clearing the cache while a block is currently executing
  for (int i = 0; i < MAX_BLOCKS; i++) {
    SH4BlockEntry *block = &blocks_[i];
    block->run = default_block_;
    block->flags = 0;
  }
}

void SH4CodeCache::ClearBlocks() {
  // unlink all block pointers and remove all map entries. this is only safe to
  // use when no blocks are currently executing
  for (int i = 0; i < MAX_BLOCKS; i++) {
    SH4BlockEntry *block = &blocks_[i];
    block->run = default_block_;
    block->flags = 0;
    block->it = block_map_.end();
    block->rit = reverse_block_map_.end();
  }
  block_map_.clear();
  reverse_block_map_.clear();

  // have the backend reset its codegen buffers as well
  backend_->Reset();
}

bool SH4CodeCache::HandleException(void *ctx, Exception &ex) {
  SH4CodeCache *self = reinterpret_cast<SH4CodeCache *>(ctx);

  // see if there is an assembled block corresponding to the current pc
  SH4BlockEntry *block = self->LookupBlockReverse(ex.pc);
  if (!block) {
    return false;
  }

  // let the backend attempt to handle the exception
  if (!self->backend_->HandleFastmemException(ex)) {
    return false;
  }

  // exception was handled, unlink the block pointer and flag the block to be
  // recompiled without fastmem optimizations on the next access. note, the
  // block can't be removed from the lookup maps at this point because it's
  // still executing and may trigger subsequent exceptions
  block->run = self->default_block_;
  block->flags |= BF_SLOWMEM;

  return true;
}

SH4BlockEntry *SH4CodeCache::LookupBlock(uint32_t guest_addr) {
  // find the first block who's address is greater than guest_addr
  auto it = block_map_.upper_bound(guest_addr);

  // if all addresses are are greater than guest_addr, there is no
  // block for this address
  if (it == block_map_.begin()) {
    return nullptr;
  }

  // host_addr belongs to the block before
  return (--it)->second;
}

SH4BlockEntry *SH4CodeCache::LookupBlockReverse(uintptr_t host_addr) {
  auto it = reverse_block_map_.upper_bound(host_addr);

  if (it == reverse_block_map_.begin()) {
    return nullptr;
  }

  return (--it)->second;
}
