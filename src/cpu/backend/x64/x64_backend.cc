#include <xbyak/xbyak.h>
#include "cpu/backend/x64/x64_backend.h"
#include "cpu/backend/x64/x64_block.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

X64Backend::X64Backend(emu::Memory &memory) : Backend(memory) {}

X64Backend::~X64Backend() {}

bool X64Backend::Init() { return true; }

std::unique_ptr<RuntimeBlock> X64Backend::AssembleBlock(IRBuilder &builder) {
  int guest_cycles = 0;

  for (auto block : builder.blocks()) {
    for (auto instr : block->instrs()) {
      if (instr->op() == OP_ADD) {
      } else if (instr->op() == OP_BRANCH) {
      }
    }
  }

  return std::unique_ptr<RuntimeBlock>(new X64Block(guest_cycles));
}
