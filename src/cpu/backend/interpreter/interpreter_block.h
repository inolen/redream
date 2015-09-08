#ifndef INTERPRETER_BLOCK_H
#define INTERPRETER_BLOCK_H

#include "cpu/backend/interpreter/interpreter_callbacks.h"
#include "cpu/runtime.h"
#include "emu/memory.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

union IntValue {
  int8_t i8;
  int16_t i16;
  int32_t i32;
  int64_t i64;
  float f32;
  double f64;
};

struct IntInstr {
  IntFn fn;
  IntValue arg[4];
};

class InterpreterBlock : public RuntimeBlock {
 public:
  InterpreterBlock(int guest_cycles, IntInstr *instrs, int num_instrs,
                   int locals_size);

  uint32_t Call(emu::Memory *memory, void *guest_ctx);
  void Dump();

 private:
  IntInstr *instrs_;
  int num_instrs_;
  int locals_size_;
};
}
}
}
}

#endif
