#ifndef SH4_DISASSEMBLER_H
#define SH4_DISASSEMBLER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SH4_OP_INVALID,
#define SH4_INSTR(name, desc, instr_code, cycles, flags) SH4_OP_##name,
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
  NUM_SH4_OPS,
} sh4_op_t;

typedef enum {
  SH4_FLAG_BRANCH = 0x1,
  SH4_FLAG_CONDITIONAL = 0x2,
  SH4_FLAG_DELAYED = 0x4,
  SH4_FLAG_SET_T = 0x8,
  SH4_FLAG_SET_FPSCR = 0x10,
  SH4_FLAG_SET_SR = 0x20,
} sh4_flag_t;

typedef struct {
  uint32_t addr;
  uint16_t opcode;

  sh4_op_t op;
  int cycles;
  int flags;
  uint16_t Rm;
  uint16_t Rn;
  uint16_t disp;
  uint16_t imm;
} sh4_instr_t;

bool sh4_disasm(sh4_instr_t *i);
void sh4_format(const sh4_instr_t *i, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
