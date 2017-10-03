#include "jit/frontend/armv3/armv3_disasm.h"
#include "core/constructor.h"
#include "core/core.h"
#include "jit/frontend/armv3/armv3_fallback.h"

int armv3_optable[ARMV3_LOOKUP_SIZE];

struct jit_opdef armv3_opdefs[NUM_ARMV3_OPS] = {
#define ARMV3_INSTR(name, desc, sig, cycles, flags) \
  {ARMV3_OP_##name,                                 \
   #name,                                           \
   desc,                                            \
   #sig,                                            \
   cycles,                                          \
   flags,                                           \
   (jit_fallback)&armv3_fallback_##name},
#include "armv3_instr.inc"
#undef ARMV3_INSTR
};

static void armv3_disasm_init_lookup() {
  static int initialized = 0;

  if (initialized) {
    return;
  }

  initialized = 1;

  /* extract each opcode / opcode mask from the signature string */
  uint32_t opcodes[NUM_ARMV3_OPS] = {0};
  uint32_t opcode_masks[NUM_ARMV3_OPS] = {0};

  for (int i = 1; i < NUM_ARMV3_OPS; i++) {
    struct jit_opdef *def = &armv3_opdefs[i];

    size_t len = strlen(def->sig);

    /* 0 or 1 represents part of the opcode, anything else is a flag */
    for (size_t j = 0; j < len; j++) {
      char c = def->sig[len - j - 1];

      if (c == '0' || c == '1') {
        opcodes[i] |= (uint32_t)(c - '0') << j;
        opcode_masks[i] |= (uint32_t)1 << j;
      }
    }

    /* ignore bits outside of lookup mask */
    opcodes[i] &= ARMV3_LOOKUP_MASK;
    opcode_masks[i] &= ARMV3_LOOKUP_MASK;
  }

  /* iterate all possible lookup values, mapping a description to each */
  for (int hi = 0; hi < ARMV3_LOOKUP_SIZE_HI; hi++) {
    for (int lo = 0; lo < ARMV3_LOOKUP_SIZE_LO; lo++) {
      uint32_t instr = ARMV3_LOOKUP_INSTR(hi, lo);

      /* some operations are differentiated by having a fixed set of flags in
         the lower bits (while sharing the same encoding in the upper bits).
         due to this, operations with a more specific mask take precedence */
      int prev_bits = 0;

      for (int i = 1; i < NUM_ARMV3_OPS; i++) {
        uint32_t opcode = opcodes[i];
        uint32_t opcode_mask = opcode_masks[i];

        if ((instr & opcode_mask) == opcode) {
          int bits = popcnt32(opcode_mask);

          CHECK_NE(bits, prev_bits);

          if (bits > prev_bits) {
            armv3_optable[ARMV3_LOOKUP_INDEX(instr)] = i;
            prev_bits = bits;
          }
        }
      }
    }
  }
}

int32_t armv3_disasm_offset(uint32_t offset) {
  int32_t res = offset;
  if (res & 0x00800000) {
    res |= 0xff000000;
  }
  res <<= 2;
  return res;
}

void armv3_disasm_shift(uint32_t shift, enum armv3_shift_source *src,
                        enum armv3_shift_type *type, uint32_t *n) {
  *src = shift & 0x1;
  *type = (shift >> 1) & 0x3;

  if (*src) {
    /* shift amount specified in register */
    *n = shift >> 4;
  } else {
    /* shift amount specified imm */
    *n = shift >> 3;

    /* special cases */
    if (*type == SHIFT_LSR && *n == 0) {
      *n = 32;
    } else if (*type == SHIFT_ASR && *n == 0) {
      *n = 32;
    } else if (*type == SHIFT_ROR && *n == 0) {
      *type = SHIFT_RRX;
      *n = 1;
    }
  }
}

static const char *armv3_format_reg[] = {"r0", "r1", "r2", "r3", "r4",  "r5",
                                         "r6", "r7", "r8", "r9", "r10", "r11",
                                         "ip", "sp", "lr", "pc"};

static const char *armv3_format_cond[] = {"eq", "ne", "cs", "cc", "mi", "pl",
                                          "vs", "vc", "hi", "ls", "ge", "lt",
                                          "gt", "le", "",   "nv"};

static const char *armv3_format_shift[] = {"lsl", "lsr", "asr", "ror", "rrx"};

static const char *armv3_format_psr[] = {"CPSR", "SPSR"};

#define ROR_IMM(data, n) \
  (uint32_t)(((uint64_t)(data) << (32 - (n))) | ((uint64_t)(data) >> (n)))

void armv3_format(uint32_t addr, uint32_t instr, char *buffer,
                  size_t buffer_size) {
  struct jit_opdef *def = armv3_get_opdef(instr);
  union armv3_instr i = {instr};

  char value[128];
  size_t value_len;

  /* copy initial formatted description */
  snprintf(buffer, buffer_size, "0x%08x  %s", addr, def->desc);

  /* cond */
  int cond = i.raw >> 28;
  value_len = snprintf(value, sizeof(value), "%s", armv3_format_cond[cond]);
  strnrep(buffer, buffer_size, "{cond}", 6, value, value_len);

  if (def->flags & FLAG_SET_PC) {
    /* expr */
    int32_t offset = armv3_disasm_offset(i.branch.offset);
    uint32_t dest = addr + 8 /* account for prefetch */ + offset;
    value_len = snprintf(value, sizeof(value), "#0x%x", dest);
    strnrep(buffer, buffer_size, "{expr}", 6, value, value_len);
  }

  if (def->flags & FLAG_DATA) {
    /* s */
    value_len = snprintf(value, sizeof(value), "%s", i.data.s ? "s" : "");
    strnrep(buffer, buffer_size, "{s}", 3, value, value_len);

    /* rd */
    value_len =
        snprintf(value, sizeof(value), "%s", armv3_format_reg[i.data.rd]);
    strnrep(buffer, buffer_size, "{rd}", 4, value, value_len);

    /* rn */
    value_len =
        snprintf(value, sizeof(value), "%s", armv3_format_reg[i.data.rn]);
    strnrep(buffer, buffer_size, "{rn}", 4, value, value_len);

    /* expr */
    if (i.data.i) {
      uint32_t data = i.data_imm.imm;
      if (i.data_imm.rot) {
        data = ROR_IMM(data, i.data_imm.rot << 1);
      }
      value_len = snprintf(value, sizeof(value), "#%d", data);
    } else {
      enum armv3_shift_source src;
      enum armv3_shift_type type;
      uint32_t n;
      armv3_disasm_shift(i.data_reg.shift, &src, &type, &n);

      value_len = 0;
      value_len += snprintf(value + value_len, sizeof(value) - value_len, "%s",
                            armv3_format_reg[i.data_reg.rm]);

      if (src == SHIFT_IMM) {
        if (n) {
          value_len += snprintf(value + value_len, sizeof(value) - value_len,
                                ", %s #%d", armv3_format_shift[type], n);
        }
      } else {
        value_len +=
            snprintf(value + value_len, sizeof(value) - value_len, ", %s %s",
                     armv3_format_shift[type], armv3_format_reg[n]);
      }
    }
    strnrep(buffer, buffer_size, "{expr}", 6, value, value_len);
  }

  if (def->flags & FLAG_PSR) {
    if (def->op == ARMV3_OP_MRS) {
      /* rd */
      value_len =
          snprintf(value, sizeof(value), "%s", armv3_format_reg[i.mrs.rd]);
      strnrep(buffer, buffer_size, "{rd}", 4, value, value_len);

      /* psr */
      value_len =
          snprintf(value, sizeof(value), "%s", armv3_format_psr[i.mrs.src_psr]);
      strnrep(buffer, buffer_size, "{psr}", 5, value, value_len);
    } else {
      /* psr */
      if (i.msr.all) {
        value_len = snprintf(value, sizeof(value), "%s",
                             armv3_format_psr[i.msr.dst_psr]);
      } else {
        value_len = snprintf(value, sizeof(value), "%s_flg",
                             armv3_format_psr[i.msr.dst_psr]);
      }
      strnrep(buffer, buffer_size, "{psr}", 5, value, value_len);

      /* expr */
      if (i.msr.i) {
        uint32_t data = ROR_IMM(i.msr_imm.imm, i.msr_imm.rot << 1);
        value_len = snprintf(value, sizeof(value), "#0x%x", data);
      } else {
        value_len = snprintf(value, sizeof(value), "%s",
                             armv3_format_reg[i.msr_reg.rm]);
      }
      strnrep(buffer, buffer_size, "{expr}", 6, value, value_len);
    }
  }

  if (def->flags & FLAG_MUL) {
    /* TODO */
  }

  if (def->flags & FLAG_XFR) {
    /* b */
    value_len = snprintf(value, sizeof(value), "%s", i.xfr.b ? "b" : "");
    strnrep(buffer, buffer_size, "{b}", 3, value, value_len);

    /* t */
    value_len = snprintf(value, sizeof(value), "%s", i.xfr.w ? "b" : "");
    strnrep(buffer, buffer_size, "{t}", 3, value, value_len);

    /* rd */
    value_len =
        snprintf(value, sizeof(value), "%s", armv3_format_reg[i.xfr.rd]);
    strnrep(buffer, buffer_size, "{rd}", 4, value, value_len);

    /* addr */
    {
      value_len = 0;
      value_len += snprintf(value + value_len, sizeof(value) - value_len, "[%s",
                            armv3_format_reg[i.xfr.rn]);

      if (!i.xfr.p) {
        /* post-indexing */
        value_len +=
            snprintf(value + value_len, sizeof(value) - value_len, "]");
      }

      if (!i.xfr.u) {
        /* subtract offset */
        value_len +=
            snprintf(value + value_len, sizeof(value) - value_len, "-");
      }

      if (i.xfr.i) {
        /* offset reg */
        value_len +=
            snprintf(value + value_len, sizeof(value) - value_len, ", %s%s",
                     i.xfr.u ? "" : "-", armv3_format_reg[i.xfr_reg.rm]);

        enum armv3_shift_source src;
        enum armv3_shift_type type;
        uint32_t n;
        armv3_disasm_shift(i.xfr_reg.shift, &src, &type, &n);

        if (src == SHIFT_IMM) {
          if (n) {
            value_len += snprintf(value + value_len, sizeof(value) - value_len,
                                  ", %s #%d", armv3_format_shift[type], n);
          }
        } else {
          value_len +=
              snprintf(value + value_len, sizeof(value) - value_len, ", %s %s",
                       armv3_format_shift[type], armv3_format_reg[n]);
        }
      } else {
        /* offset imm */
        if (i.xfr_imm.imm) {
          value_len += snprintf(value + value_len, sizeof(value) - value_len,
                                ", #%d", i.xfr_imm.imm);
        }
      }

      if (i.xfr.p) {
        /* pre-indexing */
        value_len +=
            snprintf(value + value_len, sizeof(value) - value_len, "]");

        if (i.xfr.w) {
          /* writeback */
          value_len +=
              snprintf(value + value_len, sizeof(value) - value_len, "!");
        }
      }

      strnrep(buffer, buffer_size, "{addr}", 6, value, value_len);
    }
  }

  if (def->flags & FLAG_BLK) {
    /* TODO */
  }

  if (def->flags & FLAG_SWP) {
    /* TODO */
  }

  if (def->flags & FLAG_SWI) {
    /* TODO */
  }
}

CONSTRUCTOR(armv3_disasm_init) {
  armv3_disasm_init_lookup();
}
