#include "cpu/backend/x64/x64_backend.h"
#include "cpu/backend/x64/x64_block.h"
#include "emu/profiler.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::ir;

X64Block::X64Block(int guest_cycles, X64Fn fn)
    : RuntimeBlock(guest_cycles), fn_(fn) {}

X64Block::~X64Block() {}

uint32_t X64Block::Call(emu::Memory *memory, void *guest_ctx) {
  fn_(guest_ctx);
  return 0xdeadbeef;
}
