#include "jit/frontend/sh4/sh4_instr.h"

namespace dreavm {
namespace jit {
namespace frontend {
namespace sh4 {

InstrType instrs[NUM_OPCODES] = {
#define SH4_INSTR(name, instr_code, cycles, flags) \
  { #name, OP_##name, #instr_code, cycles, flags } \
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
}
}
}
}
