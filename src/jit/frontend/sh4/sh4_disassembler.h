#ifndef SH4_DISASSEMBLER_H
#define SH4_DISASSEMBLER_H

#include <stddef.h>
#include <stdint.h>

namespace re {
namespace jit {
namespace frontend {
namespace sh4 {

enum Op {
  OP_INVALID,
#define SH4_INSTR(name, desc, instr_code, cycles, flags) OP_##name,
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
  NUM_OPCODES,
};

enum OpFlag {
  OP_FLAG_BRANCH = 0x1,
  OP_FLAG_CONDITIONAL = 0x2,
  OP_FLAG_DELAYED = 0x4,
  OP_FLAG_SET_T = 0x8,
  OP_FLAG_SET_FPSCR = 0x10,
  OP_FLAG_SET_SR = 0x20,
};

struct Instr {
  uint32_t addr;
  uint16_t opcode;

  Op op;
  int cycles;
  int flags;
  uint16_t Rm;
  uint16_t Rn;
  uint16_t disp;
  uint16_t imm;
};

class SH4Disassembler {
 public:
  static bool Disasm(Instr *i);
  static void Format(const Instr &i, char *buffer, size_t buffer_size);
};
}
}
}
}

#endif
