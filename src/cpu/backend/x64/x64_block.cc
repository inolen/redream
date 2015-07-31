#include <iomanip>
#include <beaengine/BeaEngine.h>
#include "cpu/backend/x64/x64_backend.h"
#include "cpu/backend/x64/x64_block.h"
#include "emu/profiler.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::ir;

X64Block::X64Block(int guest_cycles, X64Fn fn)
    : RuntimeBlock(guest_cycles), fn_(fn) {}

X64Block::~X64Block() {}

uint32_t X64Block::Call(emu::Memory *memory, void *guest_ctx) {
  return fn_(guest_ctx, memory);
}

void X64Block::Dump() {
  DISASM dsm;
  dsm.Archi = 64;
  dsm.EIP = (uintptr_t)fn_;
  dsm.SecurityBlock = 0;

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
