#include <sstream>
#include <gtest/gtest.h>
#include "jit/ir/passes/dead_code_elimination_pass.h"
#include "jit/ir/ir_builder.h"
#include "jit/ir/ir_reader.h"
#include "jit/ir/ir_writer.h"

using namespace re;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;

TEST(DeadCodeEliminationPassTest, Sanity) {
  static const char *input =
      "i32 %0 = load_context i32 0xbc\n"
      "i32 %1 = load_guest i32 %0\n"
      "i32 %2 = load_guest i32 0x8c000a10\n"
      "i32 %3 = load_guest i32 %2\n"
      "i32 %4 = load_context i32 0xc0\n"
      "i32 %5 = and i32 %3, i32 %4\n"
      "store_context i32 0xb0, i32 %5\n"
      "store_guest i32 %2, i32 %5\n"
      "i32 %6 = load_context i32 0xe4\n"
      "i32 %7 = load_guest i32 %6\n"
      "store_context i32 0xb4, i32 %7\n"
      "i64 %8 = load_context i32 0x18\n"
      "i32 %9 = load_context i32 0x38\n"
      "store_context i32 0x38, i32 %7\n"
      "i64 %10 = zext i32 %9\n"
      "i32 %11 = load_context i32 0x28\n"
      "i32 %12 = sub i32 %11, i32 0xa\n"
      "store_context i32 0x28, i32 %12\n"
      "i32 %13 = load_context i32 0x2c\n"
      "i32 %14 = add i32 %13, i32 0x7\n"
      "store_context i32 0x2c, i32 %14\n"
      "call_external i64 %8, i64 %10\n"
      "store_context i32 0x30, i32 0x8c000940\n";

  static const char *output =
      "i32 %0 = load_guest i32 0x8c000a10\n"
      "i32 %1 = load_guest i32 %0\n"
      "i32 %2 = load_context i32 0xc0\n"
      "i32 %3 = and i32 %1, i32 %2\n"
      "store_context i32 0xb0, i32 %3\n"
      "store_guest i32 %0, i32 %3\n"
      "i32 %4 = load_context i32 0xe4\n"
      "i32 %5 = load_guest i32 %4\n"
      "store_context i32 0xb4, i32 %5\n"
      "i64 %6 = load_context i32 0x18\n"
      "i32 %7 = load_context i32 0x38\n"
      "store_context i32 0x38, i32 %5\n"
      "i64 %8 = zext i32 %7\n"
      "i32 %9 = load_context i32 0x28\n"
      "i32 %10 = sub i32 %9, i32 0xa\n"
      "store_context i32 0x28, i32 %10\n"
      "i32 %11 = load_context i32 0x2c\n"
      "i32 %12 = add i32 %11, i32 0x7\n"
      "store_context i32 0x2c, i32 %12\n"
      "call_external i64 %6, i64 %8\n"
      "store_context i32 0x30, i32 0x8c000940\n";

  Arena arena(4096);
  IRBuilder builder(arena);

  IRReader reader;
  std::stringstream input_stream(input);
  reader.Parse(input_stream, builder);

  DeadCodeEliminationPass pass;
  pass.Run(builder, false);

  IRWriter writer;
  std::stringstream output_stream;
  writer.Print(builder, output_stream);

  ASSERT_STREQ(output_stream.str().c_str(), output);
}
