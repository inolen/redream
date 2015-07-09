#ifndef INTERPRETER_BACKEND_H
#define INTERPRETER_BACKEND_H

#include "cpu/backend/interpreter/interpreter_block.h"
#include "cpu/backend/backend.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

enum { SIG_V, SIG_I8, SIG_I16, SIG_I32, SIG_I64, SIG_F32, SIG_F64, SIG_NUM };
enum { IMM_ARG0 = 0x1, IMM_ARG1 = 0x2, IMM_ARG2 = 0x4, IMM_MAX = 0x8 };

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

union IntSig {
  struct {
    int result : 8;
    int arg0 : 8;
    int arg1 : 8;
    int arg2 : 8;
  };
  uint32_t full;
};

struct IntInstr {
  IntFn fn;
  IntReg arg[3];
  int result;
  intptr_t guest_addr;
  intptr_t guest_op;
};

class AssembleContext {
 public:
  AssembleContext();
  ~AssembleContext();

  IntInstr *AllocInstr();
  int AllocRegister();

  IntInstr *TranslateInstr(ir::Instr &ir_i);
  IntSig GetSignature(ir::Instr &ir_i);
  void TranslateArg(ir::Instr &ir_i, IntInstr *i, int arg, uint32_t *imm_mask);

  int max_instrs, num_instrs;
  IntInstr *instrs;
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
