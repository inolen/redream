#ifndef X64_DISASSEMBLER_H
#define X64_DISASSEMBLER_H

#include <stdbool.h>
#include <stdint.h>

struct x64_mov {
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

bool x64_decode_mov(const uint8_t *data, struct x64_mov *mov);

#endif
