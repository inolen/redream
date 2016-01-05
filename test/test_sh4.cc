#include <limits.h>
#include <memory>
#include <unordered_map>
#include "gtest/gtest.h"
#include "core/core.h"
#include "hw/sh4/sh4.h"
#include "hw/memory.h"

using namespace dreavm;
using namespace dreavm::hw;
using namespace dreavm::hw::sh4;
using namespace dreavm::jit;
using namespace dreavm::jit::frontend::sh4;

DECLARE_bool(interpreter);

void dreavm::hw::sh4::RunSH4Test(const SH4Test &test);

enum {
  UNINITIALIZED_REG = 0xbaadf00d,
};

struct SH4Test {
  const char *name;
  const uint8_t *buffer;
  uint32_t buffer_size;
  uint32_t buffer_offset;
  SH4Context in;
  SH4Context out;
};

struct SH4TestRegister {
  const char *name;
  size_t offset;
  int size;
};

static SH4TestRegister sh4_test_regs[] = {
    {"fpscr", offsetof(SH4Context, fpscr), 4},
    {"r0", offsetof(SH4Context, r[0]), 4},
    {"r1", offsetof(SH4Context, r[1]), 4},
    {"r2", offsetof(SH4Context, r[2]), 4},
    {"r3", offsetof(SH4Context, r[3]), 4},
    {"r4", offsetof(SH4Context, r[4]), 4},
    {"r5", offsetof(SH4Context, r[5]), 4},
    {"r6", offsetof(SH4Context, r[6]), 4},
    {"r7", offsetof(SH4Context, r[7]), 4},
    {"r8", offsetof(SH4Context, r[8]), 4},
    {"r9", offsetof(SH4Context, r[9]), 4},
    {"r10", offsetof(SH4Context, r[10]), 4},
    {"r11", offsetof(SH4Context, r[11]), 4},
    {"r12", offsetof(SH4Context, r[12]), 4},
    {"r13", offsetof(SH4Context, r[13]), 4},
    {"r14", offsetof(SH4Context, r[14]), 4},
    {"r15", offsetof(SH4Context, r[15]), 4},
    {"fr0", offsetof(SH4Context, fr[0]), 4},
    {"fr1", offsetof(SH4Context, fr[1]), 4},
    {"fr2", offsetof(SH4Context, fr[2]), 4},
    {"fr3", offsetof(SH4Context, fr[3]), 4},
    {"fr4", offsetof(SH4Context, fr[4]), 4},
    {"fr5", offsetof(SH4Context, fr[5]), 4},
    {"fr6", offsetof(SH4Context, fr[6]), 4},
    {"fr7", offsetof(SH4Context, fr[7]), 4},
    {"fr8", offsetof(SH4Context, fr[8]), 4},
    {"fr9", offsetof(SH4Context, fr[9]), 4},
    {"fr10", offsetof(SH4Context, fr[10]), 4},
    {"fr11", offsetof(SH4Context, fr[11]), 4},
    {"fr12", offsetof(SH4Context, fr[12]), 4},
    {"fr13", offsetof(SH4Context, fr[13]), 4},
    {"fr14", offsetof(SH4Context, fr[14]), 4},
    {"fr15", offsetof(SH4Context, fr[15]), 4},
    {"xf0", offsetof(SH4Context, xf[0]), 4},
    {"xf1", offsetof(SH4Context, xf[1]), 4},
    {"xf2", offsetof(SH4Context, xf[2]), 4},
    {"xf3", offsetof(SH4Context, xf[3]), 4},
    {"xf4", offsetof(SH4Context, xf[4]), 4},
    {"xf5", offsetof(SH4Context, xf[5]), 4},
    {"xf6", offsetof(SH4Context, xf[6]), 4},
    {"xf7", offsetof(SH4Context, xf[7]), 4},
    {"xf8", offsetof(SH4Context, xf[8]), 4},
    {"xf9", offsetof(SH4Context, xf[9]), 4},
    {"xf10", offsetof(SH4Context, xf[10]), 4},
    {"xf11", offsetof(SH4Context, xf[11]), 4},
    {"xf12", offsetof(SH4Context, xf[12]), 4},
    {"xf13", offsetof(SH4Context, xf[13]), 4},
    {"xf14", offsetof(SH4Context, xf[14]), 4},
    {"xf15", offsetof(SH4Context, xf[15]), 4},
};
int sh4_num_test_regs =
    static_cast<int>(sizeof(sh4_test_regs) / sizeof(sh4_test_regs[0]));

// clang-format off
#define INIT_CONTEXT(fpscr, r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, \
                     r12, r13, r14, r15, fr0, fr1, fr2, fr3, fr4, fr5, fr6,   \
                     fr7, fr8, fr9, fr10, fr11, fr12, fr13, fr14, fr15, xf0,  \
                     xf1, xf2, xf3, xf4, xf5, xf6, xf7, xf8, xf9, xf10, xf11, \
                     xf12, xf13, xf14, xf15)                                  \
  SH4Context {                                                                \
    nullptr, nullptr, nullptr, nullptr,                                       \
    0, 0, 0, 0, 0, 0, 0, 0,                                                   \
    {r0, r1, r2,  r3,  r4,  r5,  r6,  r7,                                     \
     r8, r9, r10, r11, r12, r13, r14, r15},                                   \
    {0, 0, 0, 0, 0, 0, 0, 0}, 0,                                              \
    {fr0, fr1, fr2,  fr3,  fr4,  fr5,  fr6,  fr7,                             \
     fr8, fr9, fr10, fr11, fr12, fr13, fr14, fr15},                           \
    {xf0, xf1, xf2,  xf3,  xf4,  xf5,  xf6,  xf7,                             \
     xf8, xf9, xf10, xf11, xf12, xf13, xf14, xf15},                           \
    0, 0,                                                                     \
    { {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0 } },                  \
    0, 0, 0, fpscr                                                            \
  }

#define TEST_SH4(name, buffer, buffer_size, buffer_offset,                                                                                                               \
  fpscr_in,                                                                                                                                                              \
  r0_in,  r1_in,  r2_in,  r3_in,  r4_in,  r5_in,   r6_in,   r7_in,  r8_in,  r9_in,  r10_in,  r11_in,  r12_in,  r13_in,  r14_in,  r15_in,                                 \
  fr0_in, fr1_in, fr2_in, fr3_in, fr4_in, fr5_in,  fr6_in,  fr7_in, fr8_in, fr9_in, fr10_in, fr11_in, fr12_in, fr13_in, fr14_in, fr15_in,                                \
  xf0_in, xf1_in, xf2_in, xf3_in, xf4_in, xf5_in,  xf6_in,  xf7_in, xf8_in, xf9_in, xf10_in, xf11_in, xf12_in, xf13_in, xf14_in, xf15_in,                                \
  fpscr_out,                                                                                                                                                             \
  r0_out,  r1_out,  r2_out,  r3_out,  r4_out,  r5_out,   r6_out,   r7_out,  r8_out,  r9_out,  r10_out,  r11_out,  r12_out,  r13_out,  r14_out,  r15_out,                 \
  fr0_out, fr1_out, fr2_out, fr3_out, fr4_out, fr5_out,  fr6_out,  fr7_out, fr8_out, fr9_out, fr10_out, fr11_out, fr12_out, fr13_out, fr14_out, fr15_out,                \
  xf0_out, xf1_out, xf2_out, xf3_out, xf4_out, xf5_out,  xf6_out,  xf7_out, xf8_out, xf9_out, xf10_out, xf11_out, xf12_out, xf13_out, xf14_out, xf15_out)                \
  static SH4Test test_##name = {                                                                                                                                         \
    #name, buffer, buffer_size, buffer_offset,                                                                                                                           \
    INIT_CONTEXT(fpscr_in,                                                                                                                                               \
                 r0_in,  r1_in,  r2_in,  r3_in,  r4_in,  r5_in,   r6_in,   r7_in,  r8_in,  r9_in,  r10_in,  r11_in,  r12_in,  r13_in,  r14_in,  r15_in,                  \
                 fr0_in, fr1_in, fr2_in, fr3_in, fr4_in, fr5_in,  fr6_in,  fr7_in, fr8_in, fr9_in, fr10_in, fr11_in, fr12_in, fr13_in, fr14_in, fr15_in,                 \
                 xf0_in, xf1_in, xf2_in, xf3_in, xf4_in, xf5_in,  xf6_in,  xf7_in, xf8_in, xf9_in, xf10_in, xf11_in, xf12_in, xf13_in, xf14_in, xf15_in),                \
    INIT_CONTEXT(fpscr_out,                                                                                                                                              \
                 r0_out,  r1_out,  r2_out,  r3_out,  r4_out,  r5_out,   r6_out,   r7_out,  r8_out,  r9_out,  r10_out,  r11_out,  r12_out,  r13_out,  r14_out,  r15_out,  \
                 fr0_out, fr1_out, fr2_out, fr3_out, fr4_out, fr5_out,  fr6_out,  fr7_out, fr8_out, fr9_out, fr10_out, fr11_out, fr12_out, fr13_out, fr14_out, fr15_out, \
                 xf0_out, xf1_out, xf2_out, xf3_out, xf4_out, xf5_out,  xf6_out,  xf7_out, xf8_out, xf9_out, xf10_out, xf11_out, xf12_out, xf13_out, xf14_out, xf15_out) \
  };                                                                                                                                                                     \
  TEST(sh4_interpreter, name) {                                                                                                                                          \
    FLAGS_interpreter = true;                                                                                                                                            \
    RunSH4Test(test_##name);                                                                                                                                             \
  }                                                                                                                                                                      \
  TEST(sh4_x64, name) {                                                                                                                                                  \
    FLAGS_interpreter = false;                                                                                                                                           \
    RunSH4Test(test_##name);                                                                                                                                             \
  }
#include "test_sh4.inc"
#undef TEST_SH4
// clang-format on

namespace dreavm {
namespace hw {
namespace sh4 {

void RunSH4Test(const SH4Test &test) {
  static const uint32_t stack_address = 0x0;
  static const uint32_t stack_size = PAGE_BLKSIZE;
  static const uint32_t code_address = 0x8c010000;
  const uint32_t code_size =
      dreavm::align(test.buffer_size, static_cast<uint32_t>(PAGE_BLKSIZE));

  // setup stack and executable space in memory map
  Memory memory;
  CHECK(memory.Init());
  RegionHandle stack_handle = memory.AllocRegion(stack_address, stack_size);
  RegionHandle code_handle = memory.AllocRegion(code_address, code_size);

  MemoryMap memmap;
  memmap.Mount(stack_handle, stack_size, stack_address);
  memmap.Mount(code_handle, code_size, code_address);
  CHECK(memory.Map(memmap));

  // initialize cpu
  SH4 sh4(memory);
  CHECK(sh4.Init());

  // setup in registers
  for (int i = 0; i < sh4_num_test_regs; i++) {
    SH4TestRegister &reg = sh4_test_regs[i];

    uint32_t input = *reinterpret_cast<const uint32_t *>(
        reinterpret_cast<const uint8_t *>(&test.in) + reg.offset);

    if (input == UNINITIALIZED_REG) {
      continue;
    }

    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(&sh4.ctx_) +
                                  reg.offset) = input;
  }

  // write out RTS / NOP at 0x0 so tests will spin once the main function
  // returns to 0x0
  memory.W16(0x0, 0b0000000000001011);
  memory.W16(0x2, 0b0000000000001001);

  // setup initial stack pointer
  sh4.ctx_.r[15] = stack_size;

  // load binary
  memory.Memcpy(code_address, test.buffer, test.buffer_size);

  // skip to the test's offset
  sh4.SetPC(code_address + test.buffer_offset);

  // no instruction takes more than 8 cycles, this should be enough
  sh4.Run(code_size * 8);

  // validate out registers
  for (int i = 0; i < sh4_num_test_regs; i++) {
    SH4TestRegister &reg = sh4_test_regs[i];

    uint32_t expected = *reinterpret_cast<const uint32_t *>(
        reinterpret_cast<const uint8_t *>(&test.out) + reg.offset);

    if (expected == UNINITIALIZED_REG) {
      continue;
    }

    uint32_t actual = *reinterpret_cast<const uint32_t *>(
        reinterpret_cast<const uint8_t *>(&sh4.ctx_) + reg.offset);

    ASSERT_EQ(expected, actual) << reg.name << " expected: 0x" << std::hex
                                << expected << ", actual 0x" << actual;
  }
}
}
}
}
