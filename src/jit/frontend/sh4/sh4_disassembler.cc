#include "core/memory.h"
#include "core/string.h"
#include "hw/memory.h"
#include "jit/frontend/sh4/sh4_disassembler.h"

using namespace re;

namespace re {
namespace jit {
namespace frontend {
namespace sh4 {

static InstrType s_instrs[NUM_OPCODES] = {
#define SH4_INSTR(name, desc, sig, cycles, flags)                     \
  { OP_##name, desc, #sig, cycles, flags, 0, 0, 0, 0, 0, 0, 0, 0, 0 } \
  ,
#include "jit/frontend/sh4/sh4_instr.inc"
#undef SH4_INSTR
};
static InstrType *s_instr_lookup[UINT16_MAX] = {};

static void GetArgMask(const char *instr_code, char c, uint16_t *mask,
                       uint16_t *shift) {
  size_t len = strlen(instr_code);
  if (mask) *mask = 0;
  if (shift) *shift = 0;
  for (size_t i = 0; i < len; i++) {
    if ((!c && instr_code[i] == '1') || (c && instr_code[i] == c)) {
      if (mask) *mask |= (1 << (len - i - 1));
      if (shift) *shift = static_cast<uint16_t>(len - i - 1);
    }
  }
}

static void InitInstrTables() {
  static bool initialized = false;

  if (initialized) {
    return;
  }

  initialized = true;

  // finalize type information by extracting argument encoding information
  // from signatures
  for (int i = 0; i < NUM_OPCODES; i++) {
    InstrType *type = &s_instrs[i];
    GetArgMask(type->sig, 'i', &type->imm_mask, &type->imm_shift);
    GetArgMask(type->sig, 'd', &type->disp_mask, &type->disp_shift);
    GetArgMask(type->sig, 'm', &type->rm_mask, &type->rm_shift);
    GetArgMask(type->sig, 'n', &type->rn_mask, &type->rn_shift);
    GetArgMask(type->sig, 0, &type->opcode_mask, NULL);
  }

  // initialize lookup table
  for (unsigned w = 0; w < 0x10000; w += 0x1000) {
    for (unsigned x = 0; x < 0x1000; x += 0x100) {
      for (unsigned y = 0; y < 0x100; y += 0x10) {
        for (unsigned z = 0; z < 0x10; z++) {
          uint16_t value = w + x + y + z;

          for (unsigned i = 0; i < NUM_OPCODES; i++) {
            InstrType *type = &s_instrs[i];
            uint16_t arg_mask = type->imm_mask | type->disp_mask |
                                type->rm_mask | type->rn_mask;

            if ((value & ~arg_mask) == type->opcode_mask) {
              s_instr_lookup[value] = type;
              break;
            }
          }
        }
      }
    }
  }
}

static struct _sh4_disassembler_init {
  _sh4_disassembler_init() { InitInstrTables(); }
} sh4_disassembler_init;

bool Disasm(Instr *i) {
  InstrType *type = s_instr_lookup[i->opcode];
  if (!type) {
    i->type = nullptr;
    return false;
  }

  i->type = type;
  i->Rm = (i->opcode & type->rm_mask) >> type->rm_shift;
  i->Rn = (i->opcode & type->rn_mask) >> type->rn_shift;
  i->disp = (i->opcode & type->disp_mask) >> type->disp_shift;
  i->imm = (i->opcode & type->imm_mask) >> type->imm_shift;
  return true;
}

void Dump(const void *data, size_t size, uint32_t base) {
  Instr instr;
  char buffer[128];
  char value[128];
  size_t value_len;
  uint32_t movsize;
  uint32_t pcmask;

  for (size_t i = 0; i < size; i += 2) {
    instr.addr = base + i;
    instr.opcode =
        re::load<uint16_t>(reinterpret_cast<const uint8_t *>(data) + i);

    if (!Disasm(&instr)) {
      snprintf(buffer, sizeof(buffer), "%08x  .word 0x%04x", instr.addr,
               instr.opcode);
    } else {
      // copy initial formatted description
      snprintf(buffer, sizeof(buffer), "%08x  %s", instr.addr,
               instr.type->desc);

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
      value_len =
          snprintf(value, sizeof(value), "(0x%x,rn)", instr.disp * movsize);
      CHECK_EQ(
          strnrep(buffer, sizeof(buffer), "(disp:4,rn)", 11, value, value_len),
          0);

      // (disp:4,rm)
      value_len =
          snprintf(value, sizeof(value), "(0x%x,rm)", instr.disp * movsize);
      CHECK_EQ(
          strnrep(buffer, sizeof(buffer), "(disp:4,rm)", 11, value, value_len),
          0);

      // (disp:8,gbr)
      value_len =
          snprintf(value, sizeof(value), "(0x%x,gbr)", instr.disp * movsize);
      CHECK_EQ(
          strnrep(buffer, sizeof(buffer), "(disp:8,gbr)", 12, value, value_len),
          0);

      // (disp:8,pc)
      value_len = snprintf(value, sizeof(value), "(0x%08x)",
                           (instr.disp * movsize) + (instr.addr & pcmask) + 4);
      CHECK_EQ(
          strnrep(buffer, sizeof(buffer), "(disp:8,pc)", 11, value, value_len),
          0);

      // disp:8
      value_len = snprintf(value, sizeof(value), "0x%08x",
                           ((int8_t)instr.disp * 2) + instr.addr + 4);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "disp:8", 6, value, value_len),
               0);

      // disp:12
      value_len = snprintf(
          value, sizeof(value), "0x%08x",
          ((((int32_t)(instr.disp & 0xfff) << 20) >> 20) * 2) + instr.addr + 4);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "disp:12", 7, value, value_len),
               0);

      // drm
      value_len = snprintf(value, sizeof(value), "dr%d", instr.Rm);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "drm", 3, value, value_len), 0);

      // drn
      value_len = snprintf(value, sizeof(value), "dr%d", instr.Rn);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "drn", 3, value, value_len), 0);

      // frm
      value_len = snprintf(value, sizeof(value), "fr%d", instr.Rm);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "frm", 3, value, value_len), 0);

      // frn
      value_len = snprintf(value, sizeof(value), "fr%d", instr.Rn);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "frn", 3, value, value_len), 0);

      // fvm
      value_len = snprintf(value, sizeof(value), "fv%d", instr.Rm);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "fvm", 3, value, value_len), 0);

      // fvn
      value_len = snprintf(value, sizeof(value), "fv%d", instr.Rn);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "fvn", 3, value, value_len), 0);

      // rm
      value_len = snprintf(value, sizeof(value), "r%d", instr.Rm);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "rm", 2, value, value_len), 0);

      // rn
      value_len = snprintf(value, sizeof(value), "r%d", instr.Rn);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "rn", 2, value, value_len), 0);

      // #imm8
      value_len = snprintf(value, sizeof(value), "0x%02x", instr.imm);
      CHECK_EQ(strnrep(buffer, sizeof(buffer), "#imm8", 5, value, value_len),
               0);
    }

    LOG_INFO(buffer);
  }
}
}
}
}
}
