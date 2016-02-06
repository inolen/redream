#ifndef INTERPRETER_BLOCK_H
#define INTERPRETER_BLOCK_H

namespace re {
namespace jit {
namespace backend {
namespace interpreter {

enum {
  MAX_INT_BLOCKS = 8192,
};

struct InterpreterBlock {
  IntInstr *instrs;
  int num_instrs;
  int locals_size;
};

extern BlockPointer int_runners[MAX_INT_BLOCKS];
extern InterpreterBlock int_blocks[MAX_INT_BLOCKS];
extern int int_num_blocks;
}
}
}
}

#endif
