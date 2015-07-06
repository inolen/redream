#ifndef INTERPRETER_BACKEND_H
#define INTERPRETER_BACKEND_H

#include "cpu/backend/interpreter/interpreter_block.h"
#include "cpu/backend/backend.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

union IntReg;

typedef uint32_t (*IntFn)(emu::Memory *memory, void *guest_ctx,
                          IntReg *registers, IntInstr *instr, uint32_t idx);

union IntReg {
  int8_t i8;
  int16_t i16;
  int32_t i32;
  int64_t i64;
  float f32;
  double f64;
};

struct IntInstr {
  IntFn fn;
  IntReg arg[3];
  int result;
  intptr_t guest_addr;
  intptr_t guest_op;
};

struct BlockRef {
  ir::Block *block;
  ptrdiff_t offset;
};

class AssembleContext {
 public:
  AssembleContext();
  ~AssembleContext();

  IntInstr *AllocInstr();
  BlockRef *AllocBlockRef();
  int AllocRegister();

  IntInstr *TranslateInstr(ir::Instr &ir_i, IntFn fn);
  void TranslateValue(ir::Value *ir_v, IntReg *r);

  int max_instrs, num_instrs;
  IntInstr *instrs;
  int max_block_refs, num_block_refs;
  BlockRef *block_refs;
  int num_registers;
};

class InterpreterBackend : public Backend {
 public:
  InterpreterBackend(emu::Memory &memory);
  ~InterpreterBackend();

  bool Init();
  std::unique_ptr<RuntimeBlock> AssembleBlock(ir::IRBuilder &builder);
};
}
}
}
}

#endif
