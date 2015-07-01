#include "cpu/backend/backend.h"
#include "cpu/frontend/frontend.h"
#include "cpu/ir/constant_propagation_pass.h"
#include "cpu/ir/context_promotion_pass.h"
#include "cpu/ir/control_flow_analysis_pass.h"
#include "cpu/ir/ir_builder.h"
#include "cpu/ir/validate_block_pass.h"
#include "cpu/runtime.h"
#include "emu/profiler.h"

using namespace dreavm;
using namespace dreavm::cpu;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

static uint8_t R8(RuntimeContext *ctx, uint32_t addr) {
  return ctx->runtime->memory().R8(addr);
}

static uint16_t R16(RuntimeContext *ctx, uint32_t addr) {
  return ctx->runtime->memory().R16(addr);
}

static uint32_t R32(RuntimeContext *ctx, uint32_t addr) {
  return ctx->runtime->memory().R32(addr);
}

static uint64_t R64(RuntimeContext *ctx, uint32_t addr) {
  return ctx->runtime->memory().R64(addr);
}

static float RF32(RuntimeContext *ctx, uint32_t addr) {
  return ctx->runtime->memory().RF32(addr);
}

static double RF64(RuntimeContext *ctx, uint32_t addr) {
  return ctx->runtime->memory().RF64(addr);
}

static void W8(RuntimeContext *ctx, uint32_t addr, uint8_t value) {
  ctx->runtime->memory().W8(addr, value);
}

static void W16(RuntimeContext *ctx, uint32_t addr, uint16_t value) {
  ctx->runtime->memory().W16(addr, value);
}

static void W32(RuntimeContext *ctx, uint32_t addr, uint32_t value) {
  ctx->runtime->memory().W32(addr, value);
}

static void W64(RuntimeContext *ctx, uint32_t addr, uint64_t value) {
  ctx->runtime->memory().W64(addr, value);
}

static void WF32(RuntimeContext *ctx, uint32_t addr, float value) {
  ctx->runtime->memory().WF32(addr, value);
}

static void WF64(RuntimeContext *ctx, uint32_t addr, double value) {
  ctx->runtime->memory().WF64(addr, value);
}

Runtime::Runtime(Memory &memory)
    : memory_(memory),
      frontend_(nullptr),
      backend_(nullptr),
      pending_reset_(false) {
  blocks_ = new std::unique_ptr<RuntimeBlock>[MAX_BLOCKS];

  runtime_ctx_.runtime = this;
  runtime_ctx_.R8 = &::R8;
  runtime_ctx_.R16 = &::R16;
  runtime_ctx_.R32 = &::R32;
  runtime_ctx_.R64 = &::R64;
  runtime_ctx_.RF32 = &::RF32;
  runtime_ctx_.RF64 = &::RF64;
  runtime_ctx_.W8 = &::W8;
  runtime_ctx_.W16 = &::W16;
  runtime_ctx_.W32 = &::W32;
  runtime_ctx_.W64 = &::W64;
  runtime_ctx_.WF32 = &::WF32;
  runtime_ctx_.WF64 = &::WF64;

  pass_runner_.AddPass(std::unique_ptr<Pass>(new ValidateBlockPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ControlFlowAnalysisPass()));
  pass_runner_.AddPass(std::unique_ptr<Pass>(new ContextPromotionPass()));
  // pass_runner_.AddPass(
  //     std::unique_ptr<Pass>(new ConstantPropagationPass(memory_)));
}

Runtime::~Runtime() { delete[] blocks_; }

bool Runtime::Init(frontend::Frontend *frontend, backend::Backend *backend) {
  frontend_ = frontend;
  backend_ = backend;

  if (!frontend_->Init()) {
    return false;
  }

  if (!backend_->Init()) {
    return false;
  }

  return true;
}

RuntimeBlock *Runtime::ResolveBlock(uint32_t addr) {
  PROFILER_SCOPE_F("runtime");

  if (pending_reset_) {
    for (int i = 0; i < MAX_BLOCKS; i++) {
      blocks_[i] = nullptr;
    }
    pending_reset_ = false;
  }

  addr = ResolveAddress(addr);
  uint32_t offset = BlockOffset(addr);
  if (offset >= MAX_BLOCKS) {
    LOG(FATAL) << "Block requested at 0x" << std::hex << addr
               << " is outside of the executable space";
  }

  RuntimeBlock *existing_block = blocks_[offset].get();
  if (existing_block) {
    return existing_block;
  }

  return CompileBlock(addr);
}

void Runtime::ResetBlocks() { pending_reset_ = true; }

uint32_t Runtime::ResolveAddress(uint32_t addr) {
  // MICROPROFILE_SCOPEI("cpu", "ResolveAddress", 0x0000ff);

  // MemoryBank *bank;
  // uint32_t offset;

  // memory_.Resolve(addr, &bank, &offset);

  // return bank->logical_addr + offset;
  return addr & 0x1fffffff;
}

RuntimeBlock *Runtime::CompileBlock(uint32_t addr) {
  PROFILER_SCOPE_F("runtime");

  LOG(INFO) << "Compiling block 0x" << std::hex << addr;

  std::unique_ptr<IRBuilder> builder = frontend_->BuildBlock(addr);
  if (!builder) {
    return nullptr;
  }

  // printf("BEFORE:\n");
  // builder->Dump();

  // run optimization passes
  pass_runner_.Run(*builder);

  // printf("AFTER:\n");
  // builder->Dump();

  std::unique_ptr<RuntimeBlock> block = backend_->AssembleBlock(*builder);
  if (!block) {
    return nullptr;
  }

  uint32_t offset = BlockOffset(addr);

  blocks_[offset] = std::move(block);

  return blocks_[offset].get();
}
