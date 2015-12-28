#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/interpreter/interpreter_block.h"

#define REP_1(l, x, r) l x r
#define REP_2(l, x, r) REP_1(l, x, r), REP_1(l, x + 1, r)
#define REP_4(l, x, r) REP_2(l, x, r), REP_2(l, x + 2, r)
#define REP_8(l, x, r) REP_4(l, x, r), REP_4(l, x + 4, r)
#define REP_16(l, x, r) REP_8(l, x, r), REP_8(l, x + 8, r)
#define REP_32(l, x, r) REP_16(l, x, r), REP_16(l, x + 16, r)
#define REP_64(l, x, r) REP_32(l, x, r), REP_32(l, x + 32, r)
#define REP_128(l, x, r) REP_64(l, x, r), REP_64(l, x + 64, r)
#define REP_256(l, x, r) REP_128(l, x, r), REP_128(l, x + 128, r)
#define REP_512(l, x, r) REP_256(l, x, r), REP_256(l, x + 256, r)
#define REP_1024(l, x, r) REP_512(l, x, r), REP_512(l, x + 512, r)
#define REP_2048(l, x, r) REP_1024(l, x, r), REP_1024(l, x + 1024, r)
#define REP_4096(l, x, r) REP_2048(l, x, r), REP_2048(l, x + 2048, r)
#define REP_8192(l, x, r) REP_4096(l, x, r), REP_4096(l, x + 4096, r)

namespace dreavm {
namespace jit {
namespace backend {
namespace interpreter {

template <int N>
static uint32_t CallBlock();

BlockRunner int_runners[MAX_INT_BLOCKS] = {REP_8192(CallBlock<, 0, >)};
InterpreterBlock int_blocks[MAX_INT_BLOCKS] = {};
int int_num_blocks = 0;

// clang-format off
#define DUFF_DEVICE_8(count, action)  \
{                                     \
  int n = (count + 7) / 8;            \
  switch (count & 7) {                \
    case 0: do { action;              \
    case 7:      action;              \
    case 6:      action;              \
    case 5:      action;              \
    case 4:      action;              \
    case 3:      action;              \
    case 2:      action;              \
    case 1:      action;              \
            } while (--n > 0);        \
  }                                   \
}
// clang-format on

// each cached interpreter block is assigned a unique instance of the CallBlock
// function. this provides two helpful features:
// 1.) guest code can be profiled with a simple sampling profiler
// 2.) provides the branch history table more context. with a single function
//     shared by all blocks, the table entry for the indirect branch in the
//     dispatch loop is constantly being thrashed. by having a unique function
//     per-block and unrolling it with a duff's device, this thrashing is
//     greatly alleviated
template <int N>
static uint32_t CallBlock() {
  InterpreterBlock *block = &int_blocks[N];

  IntInstr *instr = block->instrs;
  int num_instrs = block->num_instrs;

  int_state.sp += block->locals_size;
  CHECK_LT(int_state.sp, MAX_INT_STACK);

  DUFF_DEVICE_8(num_instrs, {
    instr->fn(instr);
    instr++;
  });

  int_state.sp -= block->locals_size;

  return int_state.pc;
}
}
}
}
}
