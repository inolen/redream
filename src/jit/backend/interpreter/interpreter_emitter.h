#ifndef INTERPRETER_EMITTER_H
#define INTERPRETER_EMITTER_H

#include "jit/ir/ir_builder.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace interpreter {

union IntValue;
struct IntInstr;

typedef uint32_t (*IntFn)(const IntInstr *instr, uint32_t idx,
                          hw::Memory *memory, IntValue *registers,
                          uint8_t *locals, void *guest_ctx);

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

class InterpreterEmitter {
 public:
  InterpreterEmitter();
  ~InterpreterEmitter();

  void Reset();

  bool Emit(ir::IRBuilder &builder, IntInstr **instr, int *num_instr,
            int *locals_size);

 private:
  uint8_t *codegen_begin_;
  uint8_t *codegen_end_;
  uint8_t *codegen_;

  uint8_t *Alloc(size_t size);
  IntInstr *AllocInstr();
  void TranslateInstr(ir::Instr &ir_i, IntInstr *i);
  void TranslateArg(ir::Instr &ir_i, IntInstr *i, int arg);
};
}
}
}
}

#endif
