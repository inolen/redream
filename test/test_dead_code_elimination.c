#include "jit/ir/ir.h"
#include "jit/passes/dead_code_elimination_pass.h"
#include "retest.h"

static uint8_t ir_buffer[1024 * 1024];
static char scratch_buffer[1024 * 1024];

/*TEST(dead_code_elimination) {
  static const char input_str[] =
      "i32 %0 = load_context i32 0xbc\n"
      "i32 %1 = load_slow i32 %0\n"
      "i32 %2 = load_slow i32 0x8c000a10\n"
      "i32 %3 = load_slow i32 %2\n"
      "i32 %4 = load_context i32 0xc0\n"
      "i32 %5 = and i32 %3, i32 %4\n"
      "store_context i32 0xb0, i32 %5\n"
      "store_slow i32 %2, i32 %5\n"
      "i32 %6 = load_context i32 0xe4\n"
      "i32 %7 = load_slow i32 %6\n"
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
      "call i64 %8, i64 %10\n"
      "store_context i32 0x30, i32 0x8c000940\n";

  static const char output_str[] =
      "i32 %0 = load_slow i32 0x8c000a10\n"
      "i32 %1 = load_slow i32 %0\n"
      "i32 %2 = load_context i32 0xc0\n"
      "i32 %3 = and i32 %1, i32 %2\n"
      "store_context i32 0xb0, i32 %3\n"
      "store_slow i32 %0, i32 %3\n"
      "i32 %4 = load_context i32 0xe4\n"
      "i32 %5 = load_slow i32 %4\n"
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
      "call i64 %6, i64 %8\n"
      "store_context i32 0x30, i32 0x8c000940\n";

  struct ir ir = {0};
  ir.buffer = ir_buffer;
  ir.capacity = sizeof(ir_buffer);

  FILE *input = tmpfile();
  fwrite(input_str, 1, sizeof(input_str) - 1, input);
  rewind(input);
  int res = ir_read(input, &ir);
  fclose(input);
  CHECK(res);

  struct dce *dce = dce_create();
  dce_run(dce, &ir);
  dce_destroy(dce);

  FILE *output = tmpfile();
  ir_write(&ir, output);
  rewind(output);
  size_t n = fread(&scratch_buffer, 1, sizeof(scratch_buffer), output);
  fclose(output);
  CHECK_NE(n, 0u);

  CHECK_STREQ(scratch_buffer, output_str);
}*/
