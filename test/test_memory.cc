#include "gtest/gtest.h"
#include "core/core.h"
#include "emu/memory.h"

using namespace dreavm;
using namespace dreavm::core;
using namespace dreavm::emu;

enum {
  NUM_AREAS = 8,
  AREA_SIZE = 0x04000000,
  PHYSICAL_SIZE = NUM_AREAS * AREA_SIZE,  // 0x00000000-0x1fffffff
};

class MemoryTest : public ::testing::Test {
 protected:
  static uint8_t R8(void *ctx, uint32_t addr) {
    MemoryTest *test = reinterpret_cast<MemoryTest *>(ctx);
    return test->areas_[0][addr & ~0xc0000000];
  }
  static uint16_t R16(void *ctx, uint32_t addr) {
    MemoryTest *test = reinterpret_cast<MemoryTest *>(ctx);
    return *(uint16_t *)&test->areas_[0][addr & ~0xc0000000];
  }
  static uint32_t R32(void *ctx, uint32_t addr) {
    MemoryTest *test = reinterpret_cast<MemoryTest *>(ctx);
    return *(uint32_t *)&test->areas_[0][addr & ~0xc0000000];
  }
  static uint64_t R64(void *ctx, uint32_t addr) {
    MemoryTest *test = reinterpret_cast<MemoryTest *>(ctx);
    return *(uint64_t *)&test->areas_[0][addr & ~0xc0000000];
  }
  static void W8(void *ctx, uint32_t addr, uint8_t value) {
    MemoryTest *test = reinterpret_cast<MemoryTest *>(ctx);
    test->areas_[0][addr & ~0xc0000000] = value;
  }
  static void W16(void *ctx, uint32_t addr, uint16_t value) {
    MemoryTest *test = reinterpret_cast<MemoryTest *>(ctx);
    *(uint16_t *)&test->areas_[0][addr & ~0xc0000000] = value;
  }
  static void W32(void *ctx, uint32_t addr, uint32_t value) {
    MemoryTest *test = reinterpret_cast<MemoryTest *>(ctx);
    *(uint32_t *)&test->areas_[0][addr & ~0xc0000000] = value;
  }
  static void W64(void *ctx, uint32_t addr, uint64_t value) {
    MemoryTest *test = reinterpret_cast<MemoryTest *>(ctx);
    *(uint64_t *)&test->areas_[0][addr & ~0xc0000000] = value;
  }

  void SetUp() {
    memset(areas_, 0, sizeof(areas_));

    for (int i = 0; i < NUM_AREAS; i++) {
      areas_[i] = (uint8_t *)calloc(AREA_SIZE, sizeof(uint8_t));
    }

    // P0 consists of the actual physical address space broken into NUM_AREAS
    // each of AREA_SIZE, as well as 3 mirrors of the entire physical space
    // P1 mirrors all of P0
    // P2 mirrors all of P0
    for (int i = 0, start = 0x0; i < NUM_AREAS; i++, start += AREA_SIZE) {
      memory_.Mount(start, start + AREA_SIZE - 1, 0xe0000000, areas_[i]);
    }

    // setup handlers at the beginning of P3
    memory_.Handle(0xc0000000, 0xc0000000 + PHYSICAL_SIZE - 1, 0x0, this,
                   &MemoryTest::R8, &MemoryTest::R16, &MemoryTest::R32,
                   &MemoryTest::R64, &MemoryTest::W8, &MemoryTest::W16,
                   &MemoryTest::W32, &MemoryTest::W64);
  }

  void TearDown() {
    for (unsigned i = 0; i < NUM_AREAS; i++) {
      free(areas_[i]);
    }
  }

  Memory memory_;
  uint8_t *areas_[NUM_AREAS];
};

TEST(Memory, L1) {
  PageTable table;
  table.MapRange(0x0000, 0x2fff, 0x0, 1);
  CHECK_EQ(1, table.Lookup(0x0));
  CHECK_EQ(1, table.Lookup(0x1000));
  CHECK_EQ(1, table.Lookup(0x2000));
  CHECK_EQ(UNMAPPED, table.Lookup(0x3000));
}

TEST_F(MemoryTest, Mounts) {
  MemoryBank *bank;
  uint32_t offset;

  // resolve P0 A0 physical address
  memory_.Resolve(0xff, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[0]);
  CHECK_EQ(offset, (uint32_t)0xff);

  // resolve P0 A1 physical address
  memory_.Resolve(0x040000ff, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[1]);
  CHECK_EQ(offset, (uint32_t)0xff);

  // resolve back edge of P0 A6
  memory_.Resolve(0x1bffffff, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[6]);
  CHECK_EQ(offset, (uint32_t)0x03ffffff);

  // resolve front edge of P0 A7
  memory_.Resolve(0x1c000000, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[7]);
  CHECK_EQ(offset, (uint32_t)0x0);

  // resolve back edge of P0 A7
  memory_.Resolve(0x1fffffff, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[7]);
  CHECK_EQ(offset, (uint32_t)0x03ffffff);
}

TEST_F(MemoryTest, Mirror) {
  MemoryBank *bank;
  uint32_t offset;

  // resolve P1 A0 mirror to P0 A0 physical address
  memory_.Resolve(0x200000ff, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[0]);
  CHECK_EQ(offset, (uint32_t)0xff);

  // resolve P1 A1 mirror to P0 A1 physical address
  memory_.Resolve(0x240000ff, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[1]);
  CHECK_EQ(offset, offset);

  // resolve P2 A0 address to P0 A0 physical address
  memory_.Resolve(0xa00000ff, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[0]);
  CHECK_EQ(offset, (uint32_t)0xff);

  // resolve back edge of P1 A6 to P0 A6
  memory_.Resolve(0x3bffffff, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[6]);
  CHECK_EQ(offset, (uint32_t)0x03ffffff);

  // resolve front edge of P1 A7 to P0 A7
  memory_.Resolve(0x3c000000, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[7]);
  CHECK_EQ(offset, (uint32_t)0x0);

  // resolve back edge of P1 A7 to P0 A7
  memory_.Resolve(0x3fffffff, &bank, &offset);
  CHECK_EQ(bank->physical_addr, areas_[7]);
  CHECK_EQ(offset, (uint32_t)0x03ffffff);
}

TEST_F(MemoryTest, Handlers) {
  MemoryBank *bank;
  uint32_t offset;

  // resolve P2 A0 mirror to dynamic bank entry
  memory_.Resolve(0xc00000ff, &bank, &offset);
  CHECK_EQ((intptr_t)bank->physical_addr, 0);
  CHECK_EQ(bank->logical_addr, 0xc0000000);
  CHECK_EQ(offset, (uint32_t)0xff);
}

TEST_F(MemoryTest, Read) {
  // read from valid address in P2 A2
  areas_[2][0xff] = 13;
  CHECK_EQ(memory_.R8(0xa80000ff), 13);
}

TEST_F(MemoryTest, Write) {
  // write to valid address in P2 A2
  memory_.W8(0xa80000ff, 13);
  CHECK_EQ(areas_[2][0xff], 13);
}
