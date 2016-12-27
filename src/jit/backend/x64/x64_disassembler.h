#ifndef X64_DISASSEMBLER_H
#define X64_DISASSEMBLER_H

#include <stdint.h>

struct x64_mov {
  int length;
  int is_load;
  int is_indirect;
  int has_imm;
  int has_base;
  int has_index;
  int operand_size;
  int reg;
  int base;
  int index;
  int scale;
  int disp;
  uint64_t imm;
};

int x64_decode_mov(const uint8_t *data, struct x64_mov *mov);

#endif
