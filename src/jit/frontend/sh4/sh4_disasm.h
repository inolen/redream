#ifndef SH4_DISASM_H
#define SH4_DISASM_H

#include <stddef.h>
#include <stdint.h>

enum {
  SH4_FLAG_LOAD = 0x1,
  SH4_FLAG_STORE = 0x2,
  SH4_FLAG_BRANCH = 0x4,
  SH4_FLAG_CONDITIONAL = 0x8,
  SH4_FLAG_DELAYED = 0x10,
  SH4_FLAG_SET_T = 0x20,
  SH4_FLAG_SET_FPSCR = 0x40,
  SH4_FLAG_SET_SR = 0x80,
};

enum sh4_op {
#define SH4_INSTR(name, desc, instr_code, cycles, flags) SH4_OP_##name,
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
  NUM_SH4_OPS,
};

struct sh4_opdef {
  enum sh4_op op;
  const char *name;
  const char *desc;
  const char *sig;
  int cycles;
  int flags;
  uint16_t opcode_mask;
  uint16_t imm_mask, imm_shift;
  uint16_t disp_mask, disp_shift;
  uint16_t rm_mask, rm_shift;
  uint16_t rn_mask, rn_shift;
};

struct sh4_instr {
  uint32_t addr;
  uint16_t opcode;

  enum sh4_op op;
  int cycles;
  int flags;
  uint16_t Rm;
  uint16_t Rn;
  uint16_t disp;
  uint16_t imm;
};

extern struct sh4_opdef sh4_opdefs[NUM_SH4_OPS];

int sh4_disasm(struct sh4_instr *i);
void sh4_format(const struct sh4_instr *i, char *buffer, size_t buffer_size);

#endif
