#include <memory>
#include <unordered_map>
#include "gtest/gtest.h"
#include "core/core.h"
#include "cpu/frontend/sh4/sh4_frontend.h"
#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/backend/x64/x64_backend.h"
#include "cpu/sh4.h"
#include "emu/memory.h"
#include "emu/scheduler.h"
#include "sh4_test.h"

using namespace dreavm;
using namespace dreavm::cpu;
using namespace dreavm::core;
using namespace dreavm::emu;

namespace dreavm {
namespace cpu {

void RunSH4Test(const SH4Test &test) {
  Scheduler scheduler;
  Memory memory;

  // initialize runtime
  frontend::sh4::SH4Frontend rt_frontend(memory);
  backend::x64::X64Backend rt_backend(memory);
  // backend::interpreter::InterpreterBackend rt_backend(memory);
  Runtime runtime(memory);
  ASSERT_TRUE(runtime.Init(&rt_frontend, &rt_backend));

  // initialize device
  SH4 sh4(scheduler, memory);
  ASSERT_TRUE(sh4.Init(&runtime));
  sh4.ctx_.pc = 0x8c010000;

  // mount the test binary and a small stack
  uint8_t stack[MAX_PAGE_SIZE];

  uint32_t binary_size = core::align(static_cast<uint32_t>(test.buffer_size),
                                     static_cast<uint32_t>(MAX_PAGE_SIZE));
  uint8_t *binary = new uint8_t[binary_size];
  memcpy(binary, test.buffer, test.buffer_size);

  memory.Mount(0x0, sizeof(stack) - 1, MIRROR_MASK, stack);
  memory.Mount(sh4.ctx_.pc, sh4.ctx_.pc + binary_size - 1, MIRROR_MASK, binary);

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

#define SH4_TEST(name, ...)     \
  TEST(SH4, name) {             \
    SH4Test test = __VA_ARGS__; \
    RunSH4Test(test);           \
  }
#include "asm/sh4_test.inc"
#undef SH4_TEST
