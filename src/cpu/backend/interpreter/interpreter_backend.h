#ifndef INTERPRETER_BACKEND_H
#define INTERPRETER_BACKEND_H

#include "cpu/backend/interpreter/interpreter_block.h"
#include "cpu/backend/backend.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

enum { NUM_INT_REGS = 4 };

union IntValue;

typedef uint32_t (*IntFn)(const IntInstr *instr, uint32_t idx,
                          emu::Memory *memory, IntValue *registers,
                          uint8_t *locals, void *guest_ctx);

// signatures represent the data types for an instruction's arguments.
typedef unsigned IntSig;

enum { SIG_V, SIG_I8, SIG_I16, SIG_I32, SIG_I64, SIG_F32, SIG_F64, SIG_NUM };

static inline int GetArgSignature(IntSig sig, int arg) {
  return (sig >> (arg * 8)) & 0xf;
}

static inline void SetArgSignature(int arg, int s, IntSig *sig) {
  *sig &= ~(0xf << (arg * 8));
  *sig |= s << (arg * 8);
}

// access masks represent the location in memory of an instruction's arguments.
typedef unsigned IntAccessMask;

enum {
  // argument is located in a virtual register
  ACC_REG = 0x0,
  // argument is available as a local on the stack
  ACC_LCL = 0x1,
  // argument is encoded as an immediate in the instruction itself
  ACC_IMM = 0x2,
  // 7-bits, 2 for each argument and 1 for result
  NUM_ACC_COMBINATIONS = 1 << 7
};

static inline constexpr int GetArgAccess(IntAccessMask mask, int arg) {
  return ((mask >> (arg * 2)) & 0x3);
}

static inline void SetArgAccess(int arg, int a, IntAccessMask *mask) {
  *mask &= ~(0x3 << (arg * 2));
  *mask |= a << (arg * 2);
}

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
  intptr_t guest_addr;
  intptr_t guest_op;
};

class AssembleContext {
 public:
  AssembleContext();
  ~AssembleContext();

  IntInstr *AllocInstr();
  IntInstr *TranslateInstr(ir::Instr &ir_i);
  void TranslateArg(ir::Instr &ir_i, IntInstr *i, int arg);

  int max_instrs, num_instrs;
  IntInstr *instrs;
};

class InterpreterBackend : public Backend {
 public:
  InterpreterBackend(emu::Memory &memory);
  ~InterpreterBackend();

  const Register *registers() const;
  int num_registers() const;

  bool Init();
  std::unique_ptr<RuntimeBlock> AssembleBlock(ir::IRBuilder &builder);
};
}
}
}
}

#endif
