#ifndef SH4_DISASSEMBLER_H
#define SH4_DISASSEMBLER_H

#include <stdint.h>

namespace re {
namespace jit {
namespace frontend {
namespace sh4 {

enum Opcode {
#define SH4_INSTR(name, desc, instr_code, cycles, flags) OP_##name,
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
  NUM_OPCODES,
};

enum OpcodeFlag {
  OP_FLAG_BRANCH = 0x1,
  OP_FLAG_CONDITIONAL = 0x2,
  OP_FLAG_DELAYED = 0x4,
  OP_FLAG_SET_T = 0x8,
  OP_FLAG_SET_FPSCR = 0x10,
  OP_FLAG_SET_SR = 0x20,
};

struct InstrType {
  Opcode op;
  const char *desc;
  const char *sig;
  int cycles;
  unsigned flags;
  uint16_t opcode_mask;
  uint16_t imm_mask, imm_shift;
  uint16_t disp_mask, disp_shift;
  uint16_t rm_mask, rm_shift;
  uint16_t rn_mask, rn_shift;
};

struct Instr {
  uint32_t addr;
  uint16_t opcode;
  InstrType *type;
  uint16_t Rm;
  uint16_t Rn;
  uint16_t disp;
  uint16_t imm;
};

bool Disasm(Instr *i);
void Dump(const void *data, size_t size, uint32_t base);
}
}
}
}

#endif
