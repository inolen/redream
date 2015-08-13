#include "cpu/frontend/sh4/sh4_frontend.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::frontend::sh4;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

SH4Frontend::SH4Frontend(Memory &memory) : Frontend(memory) {}

bool SH4Frontend::Init() { return true; }

std::unique_ptr<IRBuilder> SH4Frontend::BuildBlock(uint32_t addr,
                                                   const void *guest_ctx) {
  std::unique_ptr<SH4Builder> builder(new SH4Builder(memory_));

  builder->Emit(addr, *reinterpret_cast<const SH4Context *>(guest_ctx));

  return std::unique_ptr<IRBuilder>(std::move(builder));
}
