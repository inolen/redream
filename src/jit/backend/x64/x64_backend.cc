#include "core/core.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/backend/x64/x64_block.h"

using namespace dreavm;
using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend;
using namespace dreavm::jit::backend::x64;
using namespace dreavm::jit::ir;

static const Register x64_registers[] = {{"rbx", ir::VALUE_INT_MASK},
                                         {"rbp", ir::VALUE_INT_MASK},
                                         {"r12", ir::VALUE_INT_MASK},
                                         {"r13", ir::VALUE_INT_MASK},
                                         {"r14", ir::VALUE_INT_MASK},
                                         {"r15", ir::VALUE_INT_MASK},
                                         {"xmm6", ir::VALUE_FLOAT_MASK},
                                         {"xmm7", ir::VALUE_FLOAT_MASK},
                                         {"xmm8", ir::VALUE_FLOAT_MASK},
                                         {"xmm9", ir::VALUE_FLOAT_MASK},
                                         {"xmm10", ir::VALUE_FLOAT_MASK},
                                         {"xmm11", ir::VALUE_FLOAT_MASK}};

X64Backend::X64Backend(Memory &memory)
    : Backend(memory), codegen_(1024 * 1024 * 8), emitter_(memory, codegen_) {}

X64Backend::~X64Backend() {}

const Register *X64Backend::registers() const { return x64_registers; }

int X64Backend::num_registers() const {
  return sizeof(x64_registers) / sizeof(Register);
}

void X64Backend::Reset() { codegen_.reset(); }

std::unique_ptr<RuntimeBlock> X64Backend::AssembleBlock(
    ir::IRBuilder &builder) {
  X64Fn fn = nullptr;

  // try to generate the x64 code. if the codegen buffer overflows let the
  // runtime know so it can reset the cache and try again
  try {
    fn = emitter_.Emit(builder);
  } catch (const Xbyak::Error &e) {
    if (e == Xbyak::ERR_CODE_IS_TOO_BIG) {
      return nullptr;
    }

    LOG_FATAL("X64 codegen failure, %s", e.what());
  }

  return std::unique_ptr<RuntimeBlock>(
      new X64Block(builder.guest_cycles(), fn));
}
