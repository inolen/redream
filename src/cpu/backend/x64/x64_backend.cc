#include "core/core.h"
#include "cpu/backend/x64/x64_backend.h"
#include "cpu/backend/x64/x64_block.h"

using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::cpu::backend;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

X64Backend::X64Backend(Memory &memory)
    : Backend(memory), codegen_(1024 * 1024 * 8), emitter_(memory, codegen_) {}

X64Backend::~X64Backend() {}

const Register *X64Backend::registers() const { return x64_registers; }

int X64Backend::num_registers() const {
  return sizeof(x64_registers) / sizeof(Register);
}

bool X64Backend::Init() { return true; }

void X64Backend::Reset() { codegen_.reset(); }

bool X64Backend::AssembleBlock(IRBuilder &builder, RuntimeBlock *block) {
  X64Fn fn = nullptr;

  // try to generate the x64 code. if the codegen buffer overflows let the
  // runtime know so it can reset the cache and try again
  try {
    fn = emitter_.Emit(builder);
  } catch (const Xbyak::Error &e) {
    if (e == Xbyak::ERR_CODE_IS_TOO_BIG) {
      return false;
    }

    LOG_FATAL("X64 codegen failure, %s", e.what());
  }

  block->call = &CallBlock;
  block->dump = &DumpBlock;
  block->guest_cycles = builder.guest_cycles();
  block->priv = reinterpret_cast<void *>(fn);

  return true;
}
