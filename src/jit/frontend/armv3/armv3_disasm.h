#ifndef ARMV3_DISASM_H
#define ARMV3_DISASM_H

#include <stddef.h>
#include "jit/jit_frontend.h"

enum armv3_op_flag {
  FLAG_SET_PC = 0x1,
  FLAG_DATA = 0x2,
  FLAG_PSR = 0x4,
  FLAG_MUL = 0x8,
  FLAG_XFR = 0x10,
  FLAG_BLK = 0x20,
  FLAG_SWP = 0x40,
  FLAG_SWI = 0x80,
};

enum armv3_cond_type {
  COND_EQ,
  COND_NE,
  COND_CS,
  COND_CC,
  COND_MI,
  COND_PL,
  COND_VS,
  COND_VC,
  COND_HI,
  COND_LS,
  COND_GE,
  COND_LT,
  COND_GT,
  COND_LE,
  COND_AL,
  COND_NV,
};

enum armv3_shift_source {
  SHIFT_IMM,
  SHIFT_REG,
};

enum armv3_shift_type {
  SHIFT_LSL,
  SHIFT_LSR,
  SHIFT_ASR,
  SHIFT_ROR,
  SHIFT_RRX,
};

union armv3_instr {
  uint32_t raw;

  struct {
    uint32_t offset : 24;
    uint32_t l : 1;
    uint32_t : 3;
    uint32_t cond : 4;
  } branch;

  struct {
    uint32_t op2 : 12;
    uint32_t rd : 4;
    uint32_t rn : 4;
    uint32_t s : 1;
    uint32_t op : 4;
    uint32_t i : 1;
    uint32_t : 2;
    uint32_t cond : 4;
  } data;

  struct {
    uint32_t rm : 4;
    uint32_t shift : 8;
    uint32_t rd : 4;
    uint32_t rn : 4;
    uint32_t s : 1;
    uint32_t op : 4;
    uint32_t i : 1;
    uint32_t : 2;
    uint32_t cond : 4;
  } data_reg;

  struct {
    uint32_t imm : 8;
    uint32_t rot : 4;
    uint32_t rd : 4;
    uint32_t rn : 4;
    uint32_t s : 1;
    uint32_t op : 4;
    uint32_t i : 1;
    uint32_t : 2;
    uint32_t cond : 4;
  } data_imm;

  struct {
    uint32_t : 12;
    uint32_t rd : 4;
    uint32_t : 6;
    uint32_t src_psr : 1;
    uint32_t : 5;
    uint32_t cond : 4;
  } mrs;

  struct {
    uint32_t : 16;
    uint32_t all : 1;
    uint32_t : 5;
    uint32_t dst_psr : 1;
    uint32_t : 2;
    uint32_t i : 1;
    uint32_t : 2;
    uint32_t cond : 4;
  } msr;

  struct {
    uint32_t rm : 4;
    uint32_t : 12;
    uint32_t all : 1;
    uint32_t : 5;
    uint32_t dst_psr : 1;
    uint32_t : 2;
    uint32_t i : 1;
    uint32_t : 2;
    uint32_t cond : 4;
  } msr_reg;

  struct {
    uint32_t imm : 8;
    uint32_t rot : 4;
    uint32_t : 4;
    uint32_t all : 1;
    uint32_t : 5;
    uint32_t dst_psr : 1;
    uint32_t : 2;
    uint32_t i : 1;
    uint32_t : 2;
    uint32_t cond : 4;
  } msr_imm;

  struct {
    uint32_t rm : 4;
    uint32_t : 4;
    uint32_t rs : 4;
    uint32_t rn : 4;
    uint32_t rd : 4;
    uint32_t s : 1;
    uint32_t a : 1;
    uint32_t : 6;
    uint32_t cond : 4;
  } mul;

  struct {
    uint32_t : 12;
    uint32_t rd : 4;
    uint32_t rn : 4;
    uint32_t l : 1;
    uint32_t w : 1;
    uint32_t b : 1;
    uint32_t u : 1;
    uint32_t p : 1;
    uint32_t i : 1;
    uint32_t : 2;
    uint32_t cond : 4;
  } xfr;

  struct {
    uint32_t imm : 12;
    uint32_t rd : 4;
    uint32_t rn : 4;
    uint32_t l : 1;
    uint32_t w : 1;
    uint32_t b : 1;
    uint32_t u : 1;
    uint32_t p : 1;
    uint32_t i : 1;
    uint32_t : 2;
    uint32_t cond : 4;
  } xfr_imm;

  struct {
    uint32_t rm : 4;
    uint32_t shift : 8;
    uint32_t rd : 4;
    uint32_t rn : 4;
    uint32_t l : 1;
    uint32_t w : 1;
    uint32_t b : 1;
    uint32_t u : 1;
    uint32_t p : 1;
    uint32_t i : 1;
    uint32_t : 2;
    uint32_t cond : 4;
  } xfr_reg;

  struct {
    uint32_t list : 16;
    uint32_t rn : 4;
    uint32_t l : 1;
    uint32_t w : 1;
    uint32_t s : 1;
    uint32_t u : 1;
    uint32_t p : 1;
    uint32_t : 3;
    uint32_t cond : 4;
  } xfr_blk;

  struct {
    uint32_t rm : 4;
    uint32_t : 8;
    uint32_t rd : 4;
    uint32_t rn : 4;
    uint32_t : 2;
    uint32_t b : 1;
    uint32_t : 5;
    uint32_t cond : 4;
  } swp;

  struct {
    uint32_t rlist : 16;
    uint32_t rn : 4;
    uint32_t l : 1;
    uint32_t w : 1;
    uint32_t s : 1;
    uint32_t u : 1;
    uint32_t p : 1;
    uint32_t : 3;
    uint32_t cond : 4;
  } blk;
};

enum armv3_op {
#define ARMV3_INSTR(name, desc, instr_code, cycles, flags) ARMV3_OP_##name,
#include "armv3_instr.inc"
#undef ARMV3_INSTR
  NUM_ARMV3_OPS,
};

struct armv3_desc {
  enum armv3_op op;
  const char *desc;
  const char *sig;
  int cycles;
  int flags;
};

/* most armv3 operations can be identified from bits 20-27 of the instruction.
   however, some operations share the same encoding in these upper bits (e.g.
   and & mul) differentiating only by the flags in the lower bits. because of
   this, bits 4-7 and 16-27 are needed to uniquely identify all operations */
#define ARMV3_LOOKUP_MASK 0x0fff00f0
#define ARMV3_LOOKUP_SIZE 0x10000
#define ARMV3_LOOKUP_SIZE_HI 0x1000
#define ARMV3_LOOKUP_SIZE_LO 0x10
#define ARMV3_LOOKUP_INSTR(hi, lo) ((hi << 16) | (lo << 4))
#define ARMV3_LOOKUP_INDEX(instr) \
  (((instr & 0x0fff0000) >> 12) | ((instr & 0xf0) >> 4))

extern int armv3_optable[ARMV3_LOOKUP_SIZE];
extern struct jit_opdef armv3_opdefs[NUM_ARMV3_OPS];

static inline int armv3_get_op(uint32_t instr) {
  return armv3_optable[ARMV3_LOOKUP_INDEX(instr)];
}

static struct jit_opdef *armv3_get_opdef(uint32_t instr) {
  return &armv3_opdefs[armv3_get_op(instr)];
}

int32_t armv3_disasm_offset(uint32_t offset);
void armv3_disasm_shift(uint32_t shift, enum armv3_shift_source *src,
                        enum armv3_shift_type *type, uint32_t *n);

void armv3_format(uint32_t addr, uint32_t instr, char *buffer,
                  size_t buffer_size);

#endif
