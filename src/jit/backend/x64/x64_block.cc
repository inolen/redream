#include <iomanip>
#include <sstream>
#include <beaengine/BeaEngine.h>
#include "jit/backend/x64/x64_block.h"

using namespace dreavm::hw;
using namespace dreavm::jit;
using namespace dreavm::jit::backend::x64;

X64Block::X64Block(int guest_cycles, X64Fn fn)
    : RuntimeBlock(guest_cycles, fn) {}

void X64Block::Dump() {
  DISASM dsm;
  memset(&dsm, 0, sizeof(dsm));
  dsm.Archi = 64;
  dsm.EIP = (uintptr_t)call_;
  dsm.SecurityBlock = 0;
  dsm.Options = NasmSyntax | PrefixedNumeral;

  while (true) {
    int len = Disasm(&dsm);
    if (len == OUT_OF_BLOCK) {
      LOG_INFO("Disasm engine is not allowed to read more memory");
      break;
    } else if (len == UNKNOWN_OPCODE) {
      LOG_INFO("Unknown opcode");
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
    LOG_INFO("%s %s", instr.str().c_str(), dsm.CompleteInstr);

    if (dsm.Instruction.BranchType == RetType) {
      break;
    }

    dsm.EIP = dsm.EIP + len;
  }
}