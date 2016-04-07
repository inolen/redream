#include <sstream>
#include <gtest/gtest.h>
#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/ir_builder.h"
#include "jit/ir/ir_reader.h"
#include "jit/ir/ir_writer.h"

using namespace re;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;

TEST(LoadStoreEliminationPassTest, Aliasing) {
  static const char *input =
      "store_context i32 0x104, i32 0x0\n"
      "store_context i32 0x100, i32 0x0\n"
      "store_context i32 0x10c, i32 0x0\n"
      "store_context i32 0x108, i32 0x3f800000\n"
      "i64 %0 = load_context i32 0x100\n"
      "store_context i32 0x148, i64 %0\n"
      "i64 %1 = load_context i32 0x100\n"
      "store_context i32 0x158, i64 %1\n"
      "i64 %2 = load_context i32 0x100\n"
      "store_context i32 0x160, i64 %2\n"
      "i64 %3 = load_context i32 0x100\n"
      "store_context i32 0x170, i64 %3\n"
      "store_context i32 0x104, i32 0x3f800000\n"
      "i64 %4 = load_context i32 0x100\n"
      "store_context i32 0x140, i64 %4\n"
      "i64 %5 = load_context i32 0x100\n"
      "store_context i32 0x168, i64 %5\n"
      "i64 %6 = load_context i32 0x108\n"
      "store_context i32 0x150, i64 %6\n"
      "i64 %7 = load_context i32 0x108\n"
      "store_context i32 0x178, i64 %7\n"
      "i32 %8 = load_context i32 0x2c\n"
      "i32 %9 = load_context i32 0x20\n"
      "i32 %10 = sub i32 %9, i32 0x10\n"
      "store_context i32 0x20, i32 %10\n";

  static const char *output =
      "store_context i32 0x104, i32 0x0\n"
      "store_context i32 0x100, i32 0x0\n"
      "store_context i32 0x10c, i32 0x0\n"
      "store_context i32 0x108, i32 0x3f800000\n"
      "i64 %0 = load_context i32 0x100\n"
      "store_context i32 0x148, i64 %0\n"
      "store_context i32 0x158, i64 %0\n"
      "store_context i32 0x160, i64 %0\n"
      "store_context i32 0x170, i64 %0\n"
      "store_context i32 0x104, i32 0x3f800000\n"
      "i64 %1 = load_context i32 0x100\n"
      "store_context i32 0x140, i64 %1\n"
      "store_context i32 0x168, i64 %1\n"
      "i64 %2 = load_context i32 0x108\n"
      "store_context i32 0x150, i64 %2\n"
      "store_context i32 0x178, i64 %2\n"
      "i32 %3 = load_context i32 0x2c\n"
      "i32 %4 = load_context i32 0x20\n"
      "i32 %5 = sub i32 %4, i32 0x10\n"
      "store_context i32 0x20, i32 %5\n";

  Arena arena(4096);
  IRBuilder builder(arena);

  IRReader reader;
  std::stringstream input_stream(input);
  reader.Parse(input_stream, builder);

  LoadStoreEliminationPass pass;
  pass.Run(builder, false);

  IRWriter writer;
  std::stringstream output_stream;
  writer.Print(builder, output_stream);

  ASSERT_STREQ(output_stream.str().c_str(), output);
}
