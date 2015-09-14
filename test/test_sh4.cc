#include <memory>
#include <unordered_map>
#include "gtest/gtest.h"
#include "core/core.h"
#include "hw/sh4/sh4.h"
#include "hw/memory.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/x64/x64_backend.h"
#include "sh4_test.h"

using namespace dreavm;
using namespace dreavm::hw;
using namespace dreavm::hw::sh4;
using namespace dreavm::jit;
using namespace dreavm::jit::backend::interpreter;
using namespace dreavm::jit::backend::x64;
using namespace dreavm::jit::frontend::sh4;

namespace dreavm {
namespace hw {
namespace sh4 {

template <typename BACKEND>
void RunSH4Test(const SH4Test &test) {
  static const uint32_t load_address = 0x8c010000;

  Memory memory;
  SH4Frontend rt_frontend(memory);
  BACKEND rt_backend(memory);
  Runtime runtime(memory, rt_frontend, rt_backend);

  // initialize cpu
  SH4 sh4(memory, runtime);
  sh4.Init();

  // mount a small stack (stack grows down)
  uint8_t stack[MAX_PAGE_SIZE];
  sh4.ctx_.r[15] = sizeof(stack);
  memory.Mount(0x0, sizeof(stack) - 1, ~ADDR_MASK, stack);

  // mount the test binary
  uint32_t binary_size = dreavm::align(static_cast<uint32_t>(test.buffer_size),
                                       static_cast<uint32_t>(MAX_PAGE_SIZE));
  uint8_t *binary = new uint8_t[binary_size];
  memcpy(binary, test.buffer, test.buffer_size);
  memory.Mount(load_address, load_address + binary_size - 1, ~ADDR_MASK,
               binary);

  // skip to the test's offset
  sh4.SetPC(load_address + test.offset);

  // setup in registers
  for (auto it : test.r_in) {
    SH4CTXReg &reg = sh4ctx_reg[it.first];
    memcpy((uint8_t *)&sh4.ctx_ + reg.offset, &it.second, reg.size);
  }

  sh4.Execute(UINT32_MAX);

  // cleanup binary
  delete[] binary;

  // validate out registers
  for (auto it : test.r_out) {
    SH4CTXReg &reg = sh4ctx_reg[it.first];
    uint64_t expected = 0;
    memcpy(&expected, &it.second, reg.size);
    uint64_t actual = 0;
    memcpy(&actual, (uint8_t *)&sh4.ctx_ + reg.offset, reg.size);
    ASSERT_EQ(expected, actual) << reg.name << " expected: 0x" << std::hex
                                << expected << ", actual 0x" << actual;
  }
}
}
}
}

#define SH4_TEST(name, ...)               \
  TEST(sh4_interpreter, name) {           \
    SH4Test test = __VA_ARGS__;           \
    RunSH4Test<InterpreterBackend>(test); \
  }                                       \
  TEST(sh4_x64, name) {                   \
    SH4Test test = __VA_ARGS__;           \
    RunSH4Test<X64Backend>(test);         \
  }
#include "asm/sh4_test.inc"
#undef SH4_TEST
