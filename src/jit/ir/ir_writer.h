#ifndef IR_WRITER_H
#define IR_WRITER_H

#include <sstream>
#include <unordered_map>
#include "jit/ir/ir_builder.h"

namespace dvm {
namespace jit {
namespace ir {

class IRWriter {
 public:
  void Print(const IRBuilder &builder, std::ostringstream &output);

 private:
  void PrintType(ValueTy type, std::ostringstream &output) const;
  void PrintOpcode(Opcode op, std::ostringstream &output) const;
  void PrintValue(const Value *value, std::ostringstream &output);
  void PrintInstruction(const Instr *instr, std::ostringstream &output);

  std::unordered_map<uintptr_t, int> slots_;
  int next_slot_;
};
}
}
}

#endif
