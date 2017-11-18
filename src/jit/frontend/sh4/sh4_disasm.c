#include "jit/frontend/sh4/sh4_disasm.h"
#include "core/constructor.h"
#include "core/core.h"
#include "jit/frontend/sh4/sh4_fallback.h"

int sh4_optable[UINT16_MAX + 1];

struct jit_opdef sh4_opdefs[NUM_SH4_OPS] = {
#define SH4_INSTR(name, desc, sig, cycles, flags) \
  {SH4_OP_##name,                                 \
   #name,                                         \
   desc,                                          \
   #sig,                                          \
   cycles,                                        \
   flags,                                         \
   (jit_fallback)&sh4_fallback_##name},
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
};

static void sh4_disasm_init_lookup() {
  static int initialized = 0;

  if (initialized) {
    return;
  }

  initialized = 1;

  uint16_t opcodes[NUM_SH4_OPS] = {0};
  uint16_t opcode_masks[NUM_SH4_OPS] = {0};

  for (int i = 1; i < NUM_SH4_OPS; i++) {
    struct jit_opdef *def = &sh4_opdefs[i];
    size_t len = strlen(def->sig);

    /* 0 or 1 represents part of the opcode, anything else is a flag */
    for (size_t j = 0; j < len; j++) {
      char c = def->sig[len - j - 1];

      if (c == '0' || c == '1') {
        opcodes[i] |= (uint32_t)(c - '0') << j;
        opcode_masks[i] |= (uint32_t)1 << j;
      }
    }
  }

  /* initialize lookup table */
  for (int value = 0; value <= UINT16_MAX; value++) {
    for (int i = 1 /* skip SH4_OP_INVALID */; i < NUM_SH4_OPS; i++) {
      if ((value & opcode_masks[i]) == opcodes[i]) {
        sh4_optable[value] = i;
        break;
      }
    }
  }
}

void sh4_format(uint32_t addr, union sh4_instr i, char *buffer,
                size_t buffer_size) {
  struct jit_opdef *def = sh4_get_opdef(i.raw);

  char value[128];
  size_t value_len;
  uint32_t movsize;
  uint32_t pcmask;

  /* copy initial formatted description */
  snprintf(buffer, buffer_size, "0x%08x  %s", addr, def->desc);

  /* used by mov operators with displacements */
  if (strnstr(buffer, ".b", buffer_size)) {
    movsize = 1;
    pcmask = 0xffffffff;
  } else if (strnstr(buffer, ".w", buffer_size)) {
    movsize = 2;
    pcmask = 0xffffffff;
  } else if (strnstr(buffer, ".l", buffer_size)) {
    movsize = 4;
    pcmask = 0xfffffffc;
  } else {
    movsize = 0;
    pcmask = 0;
  }

  /* (disp:4,rn) */
  value_len = snprintf(value, sizeof(value), "(0x%x,rn)", i.def.disp * movsize);
  CHECK_EQ(strnrep(buffer, buffer_size, "(disp:4,rn)", 11, value, value_len),
           0);

  /* (disp:4,rm) */
  value_len = snprintf(value, sizeof(value), "(0x%x,rm)", i.def.disp * movsize);
  CHECK_EQ(strnrep(buffer, buffer_size, "(disp:4,rm)", 11, value, value_len),
           0);

  /* (disp:8,gbr) */
  value_len =
      snprintf(value, sizeof(value), "(0x%x,gbr)", i.disp_8.disp * movsize);
  CHECK_EQ(strnrep(buffer, buffer_size, "(disp:8,gbr)", 12, value, value_len),
           0);

  /* (disp:8,pc) */
  value_len = snprintf(value, sizeof(value), "(0x%08x)",
                       (i.disp_8.disp * movsize) + (addr & pcmask) + 4);
  CHECK_EQ(strnrep(buffer, buffer_size, "(disp:8,pc)", 11, value, value_len),
           0);

  /* disp:8 */
  value_len = snprintf(value, sizeof(value), "0x%08x",
                       ((int8_t)i.disp_8.disp * 2) + addr + 4);
  CHECK_EQ(strnrep(buffer, buffer_size, "disp:8", 6, value, value_len), 0);

  /* disp:12 */
  value_len = snprintf(
      value, sizeof(value), "0x%08x",
      ((((int32_t)(i.disp_12.disp & 0xfff) << 20) >> 20) * 2) + addr + 4);
  CHECK_EQ(strnrep(buffer, buffer_size, "disp:12", 7, value, value_len), 0);

  /* drm */
  value_len = snprintf(value, sizeof(value), "dr%d", i.def.rm);
  CHECK_EQ(strnrep(buffer, buffer_size, "drm", 3, value, value_len), 0);

  /* drn */
  value_len = snprintf(value, sizeof(value), "dr%d", i.def.rn);
  CHECK_EQ(strnrep(buffer, buffer_size, "drn", 3, value, value_len), 0);

  /* frm */
  value_len = snprintf(value, sizeof(value), "fr%d", i.def.rm);
  CHECK_EQ(strnrep(buffer, buffer_size, "frm", 3, value, value_len), 0);

  /* frn */
  value_len = snprintf(value, sizeof(value), "fr%d", i.def.rn);
  CHECK_EQ(strnrep(buffer, buffer_size, "frn", 3, value, value_len), 0);

  /* fvm */
  value_len = snprintf(value, sizeof(value), "fv%d", (i.def.rm & 0x3) << 2);
  CHECK_EQ(strnrep(buffer, buffer_size, "fvm", 3, value, value_len), 0);

  /* fvn */
  value_len = snprintf(value, sizeof(value), "fv%d", (i.def.rm & 0xc));
  CHECK_EQ(strnrep(buffer, buffer_size, "fvn", 3, value, value_len), 0);

  /* rm */
  value_len = snprintf(value, sizeof(value), "r%d", i.def.rm);
  CHECK_EQ(strnrep(buffer, buffer_size, "rm", 2, value, value_len), 0);

  /* rn */
  value_len = snprintf(value, sizeof(value), "r%d", i.def.rn);
  CHECK_EQ(strnrep(buffer, buffer_size, "rn", 2, value, value_len), 0);

  /* #imm8 */
  value_len = snprintf(value, sizeof(value), "0x%02x", i.imm.imm);
  CHECK_EQ(strnrep(buffer, buffer_size, "#imm8", 5, value, value_len), 0);
}

void sh4_branch_info(uint32_t addr, union sh4_instr i, int *branch_type,
                     uint32_t *branch_addr, uint32_t *next_addr) {
  struct jit_opdef *def = sh4_get_opdef(i.raw);

  if (def->op == SH4_OP_INVALID) {
    *branch_type = SH4_BRANCH_DYNAMIC;
  } else if (def->op == SH4_OP_BF) {
    uint32_t dest_addr = ((int8_t)i.disp_8.disp * 2) + addr + 4;
    *branch_type = SH4_BRANCH_STATIC_FALSE;
    *branch_addr = dest_addr;
    *next_addr = addr + 4;
  } else if (def->op == SH4_OP_BFS) {
    uint32_t dest_addr = ((int8_t)i.disp_8.disp * 2) + addr + 4;
    *branch_type = SH4_BRANCH_STATIC_FALSE;
    *branch_addr = dest_addr;
    *next_addr = addr + 4;
  } else if (def->op == SH4_OP_BT) {
    uint32_t dest_addr = ((int8_t)i.disp_8.disp * 2) + addr + 4;
    *branch_type = SH4_BRANCH_STATIC_TRUE;
    *branch_addr = dest_addr;
    *next_addr = addr + 4;
  } else if (def->op == SH4_OP_BTS) {
    uint32_t dest_addr = ((int8_t)i.disp_8.disp * 2) + addr + 4;
    *branch_type = SH4_BRANCH_STATIC_TRUE;
    *branch_addr = dest_addr;
    *next_addr = addr + 4;
  } else if (def->op == SH4_OP_BRA) {
    /* 12-bit displacement must be sign extended */
    int32_t disp = ((i.disp_12.disp & 0xfff) << 20) >> 20;
    uint32_t dest_addr = (disp * 2) + addr + 4;
    *branch_type = SH4_BRANCH_STATIC;
    *branch_addr = dest_addr;
  } else if (def->op == SH4_OP_BRAF) {
    *branch_type = SH4_BRANCH_DYNAMIC;
  } else if (def->op == SH4_OP_BSR) {
    /* 12-bit displacement must be sign extended */
    int32_t disp = ((i.disp_12.disp & 0xfff) << 20) >> 20;
    uint32_t ret_addr = addr + 4;
    uint32_t dest_addr = ret_addr + disp * 2;
    *branch_type = SH4_BRANCH_STATIC;
    *branch_addr = dest_addr;
  } else if (def->op == SH4_OP_BSRF) {
    *branch_type = SH4_BRANCH_DYNAMIC;
  } else if (def->op == SH4_OP_JMP) {
    *branch_type = SH4_BRANCH_DYNAMIC;
  } else if (def->op == SH4_OP_JSR) {
    *branch_type = SH4_BRANCH_DYNAMIC;
  } else if (def->op == SH4_OP_RTS) {
    *branch_type = SH4_BRANCH_DYNAMIC;
  } else if (def->op == SH4_OP_RTE) {
    *branch_type = SH4_BRANCH_DYNAMIC;
  } else if (def->op == SH4_OP_SLEEP) {
    *branch_type = SH4_BRANCH_DYNAMIC;
  } else if (def->op == SH4_OP_TRAPA) {
    *branch_type = SH4_BRANCH_DYNAMIC;
  } else {
    LOG_FATAL("unexpected branch op %s", def->name);
  }
}

CONSTRUCTOR(sh4_disasm_init) {
  sh4_disasm_init_lookup();
}
