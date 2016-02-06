#ifndef X64_DISASSEMBLER_H
#define X64_DISASSEMBLER_H

#include <stdint.h>

namespace re {
namespace jit {
namespace backend {
namespace x64 {

struct X64Mov {
  int length;
  bool is_load;
  bool is_indirect;
  bool has_imm;
  bool has_base;
  bool has_index;
  int operand_size;
  int reg;
  int base;
  int index;
  int scale;
  int disp;
  uint64_t imm;
};

class X64Disassembler {
 public:
  static bool DecodeMov(const uint8_t *data, X64Mov *mov);
};
}
}
}
}

#endif
