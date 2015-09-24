#ifndef INTERPRETER_BACKEND_H
#define INTERPRETER_BACKEND_H

#include "jit/backend/backend.h"
#include "jit/runtime.h"

namespace dreavm {
namespace jit {
namespace backend {
namespace interpreter {

struct IntInstr;

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
extern const Register int_registers[];
extern const int int_num_registers;

class InterpreterBackend : public Backend {
 public:
  InterpreterBackend(hw::Memory &memory);
  ~InterpreterBackend();

  const Register *registers() const;
  int num_registers() const;

  void Reset();
  std::unique_ptr<RuntimeBlock> AssembleBlock(ir::IRBuilder &builder);

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
