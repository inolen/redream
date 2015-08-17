#ifndef INTERPRETER_BACKEND_H
#define INTERPRETER_BACKEND_H

#include "cpu/backend/backend.h"
#include "cpu/runtime.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

union IntValue;
struct IntInstr;

typedef uint32_t (*IntFn)(const IntInstr *instr, uint32_t idx,
                          emu::Memory *memory, IntValue *registers,
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

struct IntBlock {
  IntInstr *instrs;
  int num_instrs;
  int locals_size;
};

// signatures represent the data types for an instruction's arguments.
typedef unsigned IntSig;

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
  // argument is encoded as an immediate in the instruction itself
  ACC_IMM = 0x1,
  // 3-bits, 1 for each argument and 1 for result
  NUM_ACC_COMBINATIONS = 1 << 3
};

static inline constexpr int GetArgAccess(IntAccessMask mask, int arg) {
  return (mask >> arg) & 0x1;
}

static inline void SetArgAccess(int arg, int a, IntAccessMask *mask) {
  *mask &= ~(0x1 << arg);
  *mask |= a << arg;
}

// fake registers for testing register allocation
constexpr Register int_registers[] = {
    {"a", ir::VALUE_INT_MASK},   {"b", ir::VALUE_INT_MASK},
    {"c", ir::VALUE_INT_MASK},   {"d", ir::VALUE_INT_MASK},
    {"e", ir::VALUE_FLOAT_MASK}, {"f", ir::VALUE_FLOAT_MASK},
    {"g", ir::VALUE_FLOAT_MASK}, {"h", ir::VALUE_FLOAT_MASK}};

class InterpreterBackend : public Backend {
 public:
  InterpreterBackend(emu::Memory &memory);
  ~InterpreterBackend();

  const Register *registers() const;
  int num_registers() const;

  bool Init();
  void Reset();
  bool AssembleBlock(ir::IRBuilder &builder, RuntimeBlock *block);

 private:
  uint8_t *codegen_begin_;
  uint8_t *codegen_end_;
  uint8_t *codegen_;

  uint8_t *Alloc(size_t size);
  IntBlock *AllocBlock();
  IntInstr *AllocInstr();
  void TranslateInstr(ir::Instr &ir_i, IntInstr *i);
  void TranslateArg(ir::Instr &ir_i, IntInstr *i, int arg);
};
}
}
}
}

#endif
