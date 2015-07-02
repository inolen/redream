#include "cpu/backend/x64/x64_backend.h"
#include "cpu/backend/x64/x64_block.h"
#include "emu/profiler.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::ir;

X64Block::X64Block(int guest_cycles) : RuntimeBlock(guest_cycles) {}

X64Block::~X64Block() {}

uint32_t X64Block::Call(RuntimeContext &runtime_ctx) { return 0xdeadbeef; }
