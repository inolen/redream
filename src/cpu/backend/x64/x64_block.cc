#include <iomanip>
#include <sstream>
#include <beaengine/BeaEngine.h>
#include "cpu/backend/x64/x64_block.h"

using namespace dreavm::emu;

namespace dreavm {
namespace cpu {
namespace backend {
namespace x64 {

uint32_t CallBlock(RuntimeBlock *block, Memory *memory, void *guest_ctx) {
  X64Fn fn = reinterpret_cast<X64Fn>(block->priv);

  return fn(guest_ctx, memory);
}

void DumpBlock(RuntimeBlock *block) {
  X64Fn fn = reinterpret_cast<X64Fn>(block->priv);

  DISASM dsm = {};
  dsm.Archi = 64;
  dsm.EIP = (uintptr_t)fn;
  dsm.SecurityBlock = 0;
  dsm.Options = NasmSyntax | PrefixedNumeral;

  while (true) {
    int len = Disasm(&dsm);
    if (len == OUT_OF_BLOCK) {
      LOG(INFO) << "Disasm engine is not allowed to read more memory";
      break;
    } else if (len == UNKNOWN_OPCODE) {
      LOG(INFO) << "Unknown opcode";
      break;
    }

    // format instruction binary
    static const int MAX_INSTR_LENGTH = 15;
    std::stringstream instr;
    for (int i = 0; i < MAX_INSTR_LENGTH; i++) {
      uint32_t v =
          i < len ? (uint32_t) * reinterpret_cast<uint8_t *>(dsm.EIP + i) : 0;
      instr << std::hex << std::setw(2) << std::setfill('0') << v;
    }

    // print out binary / mnemonic
    LOG(INFO) << instr.str() << " " << dsm.CompleteInstr;

    if (dsm.Instruction.BranchType == RetType) {
      break;
    }

    dsm.EIP = dsm.EIP + len;
  }
}
}
}
}
}
