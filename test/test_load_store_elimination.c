#include "jit/ir/ir.h"
#include "jit/passes/load_store_elimination_pass.h"
#include "retest.h"

static uint8_t ir_buffer[1024 * 1024];
static char scratch_buffer[1024 * 1024];

/*TEST(load_store_elimination) {
  static const char input_str[] =
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

  static const char output_str[] =
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

  struct ir ir = {0};
  ir.buffer = ir_buffer;
  ir.capacity = sizeof(ir_buffer);

  FILE *input = tmpfile();
  fwrite(input_str, 1, sizeof(input_str) - 1, input);
  rewind(input);
  int res = ir_read(input, &ir);
  fclose(input);
  CHECK(res);

  struct lse *lse = lse_create();
  lse_run(lse, &ir);
  lse_destroy(lse);

  FILE *output = tmpfile();
  ir_write(&ir, output);
  rewind(output);
  size_t n = fread(&scratch_buffer, 1, sizeof(scratch_buffer), output);
  fclose(output);
  CHECK_NE(n, 0u);

  CHECK_STREQ(scratch_buffer, output_str);
}*/
