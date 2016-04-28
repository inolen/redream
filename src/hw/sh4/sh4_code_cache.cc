#include <fstream>
#include <gflags/gflags.h>
#include "core/profiler.h"
#include "hw/sh4/sh4_code_cache.h"
#include "hw/memory.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/ir/ir_builder.h"
// #include "jit/ir/passes/constant_propagation_pass.h"
// #include "jit/ir/passes/conversion_elimination_pass.h"
#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "sys/filesystem.h"

using namespace re::hw;
using namespace re::jit;
using namespace re::jit::backend;
using namespace re::jit::backend::x64;
using namespace re::jit::frontend;
using namespace re::jit::frontend::sh4;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;
using namespace re::sys;

SH4CodeCache::SH4CodeCache(const MemoryInterface &memif,
                           CodePointer default_code)
    : default_code_(default_code) {
  // add exception handler to help recompile blocks when protected memory is
  // accessed
  eh_handle_ = ExceptionHandler::instance().AddHandler(
      this, &SH4CodeCache::HandleException);

  // setup parser and emitter
  frontend_ = new SH4Frontend();
  backend_ = new X64Backend(memif);

  // setup optimization passes
  pass_runner_.AddPass(std::unique_ptr<Pass>(new LoadStoreEliminationPass()));
  // pass_runner_.AddPass(std::unique_ptr<Pass>(new ConstantPropagationPass()));
  // pass_runner_.AddPass(std::unique_ptr<Pass>(new
  // ConversionEliminationPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new DeadCodeEliminationPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new RegisterAllocationPass(
      backend_->registers(), backend_->num_registers())));

  // initialize all entries in block cache to reference the default block
  for (int i = 0; i < MAX_BLOCKS; i++) {
    code_[i] = default_code_;
  }
}

SH4CodeCache::~SH4CodeCache() {
  ExceptionHandler::instance().RemoveHandler(eh_handle_);

  delete frontend_;
  delete backend_;
}

CodePointer SH4CodeCache::CompileCode(uint32_t guest_addr, uint8_t *guest_ptr,
                                      int flags) {
  PROFILER_RUNTIME("SH4CodeCache::CompileCode");

  int offset = BLOCK_OFFSET(guest_addr);
  CHECK_LT(offset, MAX_BLOCKS);
  CodePointer &code = code_[offset];

  // make sure there's not a valid code pointer
  CHECK_EQ(code, default_code_);

  // if the block being compiled had previously been unlinked by a
  // fastmem exception, reuse the block's flags and finish removing
  // it at this time;
  auto it = blocks_.find(guest_addr);
  if (it != blocks_.end()) {
    SH4Block *unlinked = it->second;

    flags |= unlinked->flags;

    RemoveBlock(unlinked);
  }

  // translate the SH4 into IR
  int guest_size = 0;
  IRBuilder &builder =
      frontend_->TranslateCode(guest_addr, guest_ptr, flags, &guest_size);

#if 0
  const char *appdir = GetAppDir();

  char irdir[PATH_MAX];
  snprintf(irdir, sizeof(irdir), "%s" PATH_SEPARATOR "ir", appdir);
  CreateDir(irdir);

  char filename[PATH_MAX];
  snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "0x%08x.ir", irdir, guest_addr);

  std::ofstream output(filename);
  builder.Dump(output);
#endif

  pass_runner_.Run(builder);

  // assemble the IR into native code
  int host_size = 0;
  const uint8_t *host_addr = backend_->AssembleCode(builder, &host_size);

  if (!host_addr) {
    LOG_INFO("Assembler overflow, resetting block cache");

    // the backend overflowed, completely clear the block cache
    ClearBlocks();

    // if the backend fails to assemble on an empty cache, there's nothing to be
    // done
    host_addr = backend_->AssembleCode(builder, &host_size);

    CHECK(host_addr, "Backend assembler buffer overflow");
  }

  // allocate the new block
  SH4Block *block = new SH4Block();
  block->host_addr = host_addr;
  block->host_size = host_size;
  block->guest_addr = guest_addr;
  block->guest_size = guest_size;
  block->flags = flags;

  auto res = blocks_.insert(std::make_pair(block->guest_addr, block));
  CHECK(res.second);
  block->it = res.first;

  auto rres = reverse_blocks_.insert(std::make_pair(block->host_addr, block));
  CHECK(rres.second);
  block->rit = rres.first;

  // update code pointer
  code = reinterpret_cast<CodePointer>(const_cast<uint8_t *>(block->host_addr));

  return code;
}

SH4Block *SH4CodeCache::GetBlock(uint32_t guest_addr) {
  auto it = blocks_.find(guest_addr);
  if (it == blocks_.end()) {
    return nullptr;
  }
  return it->second;
}

void SH4CodeCache::RemoveBlocks(uint32_t guest_addr) {
  // remove any block which overlaps the address
  while (true) {
    SH4Block *block = LookupBlock(guest_addr);

    if (!block) {
      break;
    }

    RemoveBlock(block);
  }
}

void SH4CodeCache::UnlinkBlocks() {
  // unlink all code pointers, but don't remove the block entries. this is used
  // when clearing the cache while code is currently executing
  for (auto it : blocks_) {
    SH4Block *block = it.second;

    UnlinkBlock(block);
  }
}

void SH4CodeCache::ClearBlocks() {
  // unlink all code pointers and remove all block entries. this is only safe to
  // use when no code is currently executing
  while (blocks_.size()) {
    auto it = blocks_.begin();

    RemoveBlock(it->second);
  }

  // have the backend reset its codegen buffers as well
  backend_->Reset();
}

bool SH4CodeCache::HandleException(void *ctx, Exception &ex) {
  SH4CodeCache *self = reinterpret_cast<SH4CodeCache *>(ctx);

  // see if there is an assembled block corresponding to the current pc
  SH4Block *block =
      self->LookupBlockReverse(reinterpret_cast<const uint8_t *>(ex.pc));
  if (!block) {
    return false;
  }

  // let the backend attempt to handle the exception
  if (!self->backend_->HandleFastmemException(ex)) {
    return false;
  }

  // exception was handled, unlink the code pointer and flag the block to be
  // recompiled without fastmem optimizations on the next access. note, the
  // block can't be removed from the lookup maps at this point because it's
  // still executing and may trigger subsequent exceptions
  self->UnlinkBlock(block);

  block->flags |= SH4_SLOWMEM;

  return true;
}

SH4Block *SH4CodeCache::LookupBlock(uint32_t guest_addr) {
  // find the first block who's address is greater than guest_addr
  auto it = blocks_.upper_bound(guest_addr);

  // if all addresses are are greater than guest_addr, there is no
  // block for this address
  if (it == blocks_.begin()) {
    return nullptr;
  }

  // host_addr belongs to the block before
  return (--it)->second;
}

SH4Block *SH4CodeCache::LookupBlockReverse(const uint8_t *host_addr) {
  auto it = reverse_blocks_.upper_bound(host_addr);

  if (it == reverse_blocks_.begin()) {
    return nullptr;
  }

  return (--it)->second;
}

void SH4CodeCache::UnlinkBlock(SH4Block *block) {
  code_[BLOCK_OFFSET(block->guest_addr)] = default_code_;
}

void SH4CodeCache::RemoveBlock(SH4Block *block) {
  UnlinkBlock(block);

  blocks_.erase(block->it);
  reverse_blocks_.erase(block->rit);

  // sanity check
  CHECK_EQ(blocks_.size(), reverse_blocks_.size());

  delete block;
}
