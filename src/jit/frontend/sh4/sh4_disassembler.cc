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
}
}
}
}
