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

  // mount the test binary and a small stack
  sh4.ctx_.pc = 0x8c010000;
  memory.Alloc(0x0, 0x1fffffff, 0xe0000000);
  memory.Memcpy(sh4.ctx_.pc, test.buffer, test.buffer_size);

  // setup in registers
  // TODO we kind of need to actually use WriteMem, not write directly to the
  // context
  for (auto it : test.r_in) {
    SH4CTXReg &reg = sh4ctx_reg[it.first];
    memcpy((uint8_t *)&sh4.ctx_ + reg.offset, &it.second, reg.size);
  }

  sh4.Execute(INT64_MAX);

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
