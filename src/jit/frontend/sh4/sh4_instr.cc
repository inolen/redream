#include "core/core.h"
#include "jit/frontend/sh4/sh4_instr.h"

namespace dreavm {
namespace jit {
namespace frontend {
namespace sh4 {

InstrType instrs[NUM_OPCODES] = {
#define SH4_INSTR(name, desc, instr_code, cycles, flags) \
  { OP_##name, #name, desc, #instr_code, cycles, flags } \
  ,
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
};
InstrType *instr_lookup[UINT16_MAX];

static void InitInstrTables() {
  static bool initialized = false;

  if (initialized) {
    return;
  }

  initialized = true;

  memset(instr_lookup, 0, sizeof(instr_lookup));

  for (unsigned w = 0; w < 0x10000; w += 0x1000) {
    for (unsigned x = 0; x < 0x1000; x += 0x100) {
      for (unsigned y = 0; y < 0x100; y += 0x10) {
        for (unsigned z = 0; z < 0x10; z++) {
          uint16_t value = w + x + y + z;

          for (unsigned i = 0; i < NUM_OPCODES; i++) {
            InstrType *op = &instrs[i];

            if ((value & ~op->param_mask) == op->opcode_mask) {
              instr_lookup[value] = op;
              break;
            }
          }
        }
      }
    }
  }
}

static struct _sh4_instr_init {
  _sh4_instr_init() { InitInstrTables(); }
} sh4_instr_init;

static int strnrep(char *dst, size_t dst_size, const char *token,
                   size_t token_len, const char *value, size_t value_len) {
  char *end = dst + dst_size;

  while (char *ptr = strnstr(dst, token, dst_size)) {
    // move substring starting at the end of the token to the end of where the
    // new value will be)
    size_t dst_len = strnlen(dst, dst_size);
    size_t move_size = (dst_len + 1) - ((ptr - dst) + token_len);

    if (ptr + value_len + move_size > end) {
      return -1;
    }

    memmove(ptr + value_len, ptr + token_len, move_size);

    // copy new value into token position
    memmove(ptr, value, value_len);
  }

  return 0;
}

bool Disasm(Instr *i) {
  InstrType *type = instr_lookup[i->opcode];
  if (!type) {
    return false;
  }

  i->type = type;
  i->Rm = (i->opcode & type->Rm_mask) >> type->Rm_shift;
  i->Rn = (i->opcode & type->Rn_mask) >> type->Rn_shift;
  i->disp = (i->opcode & type->disp_mask) >> type->disp_shift;
  i->imm = (i->opcode & type->imm_mask) >> type->imm_shift;
  return true;
}

void Dump(Instr *i) {
  char buffer[128];
  char value[128];
  size_t value_len;
  uint32_t movsize;
  uint32_t pcmask;

  // copy initial formatted description
  strncpy(buffer, i->type->desc, sizeof(buffer));

  // used by mov operators with displacements
  if (strnstr(buffer, ".b", sizeof(buffer))) {
    movsize = 1;
    pcmask = 0xffffffff;
  } else if (strnstr(buffer, ".w", sizeof(buffer))) {
    movsize = 2;
    pcmask = 0xffffffff;
  } else if (strnstr(buffer, ".l", sizeof(buffer))) {
    movsize = 4;
    pcmask = 0xfffffffc;
  } else {
    movsize = 0;
    pcmask = 0;
  }

  // (disp:4,rn)
  value_len = snprintf(value, sizeof(value), "(0x%x,rn)", i->disp * movsize);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "(disp:4,rn)", 11, value, value_len),
           0);

  // (disp:4,rm)
  value_len = snprintf(value, sizeof(value), "(0x%x,rm)", i->disp * movsize);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "(disp:4,rm)", 11, value, value_len),
           0);

  // (disp:8,gbr)
  value_len = snprintf(value, sizeof(value), "(0x%x,gbr)", i->disp * movsize);
  CHECK_EQ(
      strnrep(buffer, sizeof(buffer), "(disp:8,gbr)", 12, value, value_len), 0);

  // (disp:8,pc)
  value_len = snprintf(value, sizeof(value), "(0x%08x)",
                       (i->disp * movsize) + (i->addr & pcmask) + 4);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "(disp:8,pc)", 11, value, value_len),
           0);

  // disp:8
  value_len = snprintf(value, sizeof(value), "0x%08x",
                       ((int8_t)i->disp * 2) + i->addr + 4);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "disp:8", 6, value, value_len), 0);

  // disp:12
  value_len =
      snprintf(value, sizeof(value), "0x%08x",
               (((int32_t)(i->disp & 0xfff) << 20) >> 20) + i->addr + 4);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "disp:12", 7, value, value_len), 0);

  // drm
  value_len = snprintf(value, sizeof(value), "dr%d", i->Rm);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "drm", 3, value, value_len), 0);

  // drn
  value_len = snprintf(value, sizeof(value), "dr%d", i->Rn);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "drn", 3, value, value_len), 0);

  // frm
  value_len = snprintf(value, sizeof(value), "fr%d", i->Rm);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "frm", 3, value, value_len), 0);

  // frn
  value_len = snprintf(value, sizeof(value), "fr%d", i->Rn);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "frn", 3, value, value_len), 0);

  // fvm
  value_len = snprintf(value, sizeof(value), "fv%d", i->Rm);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "fvm", 3, value, value_len), 0);

  // fvn
  value_len = snprintf(value, sizeof(value), "fv%d", i->Rn);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "fvn", 3, value, value_len), 0);

  // rm
  value_len = snprintf(value, sizeof(value), "r%d", i->Rm);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "rm", 2, value, value_len), 0);

  // rn
  value_len = snprintf(value, sizeof(value), "r%d", i->Rn);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "rn", 2, value, value_len), 0);

  // #imm8
  value_len = snprintf(value, sizeof(value), "0x%02x", i->imm);
  CHECK_EQ(strnrep(buffer, sizeof(buffer), "#imm8", 5, value, value_len), 0);

  LOG_INFO(buffer);
}
}
}
}
}
