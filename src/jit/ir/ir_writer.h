#ifndef IR_WRITER_H
#define IR_WRITER_H

#include <ostream>
#include <unordered_map>
#include "jit/ir/ir_builder.h"

namespace re {
namespace jit {
namespace ir {

class IRWriter {
 public:
  void Print(const IRBuilder &builder, std::ostream &output);

 private:
  void PrintType(ValueTy type, std::ostream &output) const;
  void PrintOp(Op op, std::ostream &output) const;
  void PrintValue(const Value *value, std::ostream &output);
  void PrintInstruction(const Instr *instr, std::ostream &output);

  std::unordered_map<uintptr_t, int> slots_;
  int next_slot_;
};
}
}
}

#endif
