#include <iomanip>
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
  dsm.Options = NasmSyntax;

  while (true) {
    int len = Disasm(&dsm);
    if (len == OUT_OF_BLOCK) {
      LOG(INFO) << "Disasm engine is not allowed to read more memory";
      break;
    } else if (len == UNKNOWN_OPCODE) {
      LOG(INFO) << "Unknown opcode";
      break;
    }

    LOG(INFO) << std::setw(2) << std::hex << std::setfill('0')
              << (int)dsm.VirtualAddr << " " << dsm.CompleteInstr;

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
