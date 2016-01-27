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
  void Print(const IRBuilder &builder);

 private:
  void PrintType(std::stringstream &ss, ValueTy type) const;
  void PrintOpcode(std::stringstream &ss, Opcode op) const;
  void PrintValue(std::stringstream &ss, const Value *value);
  void PrintInstruction(std::stringstream &ss, const Instr *instr);

  std::unordered_map<uintptr_t, int> value_ids_;
  int next_value_id_;
};
}
}
}

#endif
