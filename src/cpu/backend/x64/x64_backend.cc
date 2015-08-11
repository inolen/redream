#include "core/core.h"
#include "cpu/backend/x64/x64_backend.h"
#include "cpu/backend/x64/x64_block.h"

using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::cpu::backend;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

static Register x64_registers[] = {
    {"rbx", VALUE_INT_MASK},    {"rbp", VALUE_INT_MASK},
    {"r12", VALUE_INT_MASK},    {"r13", VALUE_INT_MASK},
    {"r14", VALUE_INT_MASK},    {"r15", VALUE_INT_MASK},
    {"xmm2", VALUE_FLOAT_MASK}, {"xmm3", VALUE_FLOAT_MASK},
    {"xmm4", VALUE_FLOAT_MASK}, {"xmm5", VALUE_FLOAT_MASK},
    {"xmm6", VALUE_FLOAT_MASK}, {"xmm7", VALUE_FLOAT_MASK}};

X64Backend::X64Backend(emu::Memory &memory)
    : Backend(memory),
      // TODO allocate a 32mb buffer for code for now, this needs to be managed
      // soon. Freed from when blocks are freed, etc.
      codegen_(1024 * 1024 * 32),
      emitter_(memory, codegen_) {}

X64Backend::~X64Backend() {}

const Register *X64Backend::registers() const { return x64_registers; }

int X64Backend::num_registers() const {
  return sizeof(x64_registers) / sizeof(Register);
}

bool X64Backend::Init() { return true; }

std::unique_ptr<RuntimeBlock> X64Backend::AssembleBlock(IRBuilder &builder) {
  X64Fn fn = emitter_.Emit(builder);

  // get number of guest cycles for this block of code
  const Value *md_guest_cycles = builder.GetMetadata(MD_GUEST_CYCLES);
  CHECK(md_guest_cycles);
  int guest_cycles = md_guest_cycles->value<int32_t>();

  return std::unique_ptr<RuntimeBlock>(new X64Block(guest_cycles, fn));
}
