#include "jit/frontend/sh4/sh4_builder.h"
#include "jit/frontend/sh4/sh4_frontend.h"

using namespace re::hw;
using namespace re::jit;
using namespace re::jit::frontend::sh4;
using namespace re::jit::ir;

SH4Frontend::SH4Frontend(Memory &memory) : Frontend(memory) {}

std::unique_ptr<IRBuilder> SH4Frontend::BuildBlock(uint32_t addr,
                                                   int max_instrs,
                                                   const void *guest_ctx) {
  std::unique_ptr<SH4Builder> builder(new SH4Builder(memory_));

  builder->Emit(addr, max_instrs,
                *reinterpret_cast<const SH4Context *>(guest_ctx));

  return std::unique_ptr<IRBuilder>(std::move(builder));
}
