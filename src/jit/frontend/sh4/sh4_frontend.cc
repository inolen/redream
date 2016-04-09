#include "jit/frontend/sh4/sh4_builder.h"
#include "jit/frontend/sh4/sh4_frontend.h"

using namespace re::jit;
using namespace re::jit::frontend::sh4;
using namespace re::jit::ir;

SH4Frontend::SH4Frontend() : arena_(4096) {}

IRBuilder &SH4Frontend::BuildBlock(uint32_t guest_addr, uint8_t *host_addr,
                                   int flags) {
  arena_.Reset();

  SH4Builder *builder = arena_.Alloc<SH4Builder>();

  new (builder) SH4Builder(arena_);

  builder->Emit(guest_addr, host_addr, flags);

  return *builder;
}
