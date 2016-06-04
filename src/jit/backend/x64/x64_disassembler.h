#ifndef X64_DISASSEMBLER_H
#define X64_DISASSEMBLER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
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
} x64_mov_t;

bool x64_decode_mov(const uint8_t *data, x64_mov_t *mov);

#ifdef __cplusplus
}
#endif

#endif
