#ifndef X64_BLOCK_H
#define X64_BLOCK_H

#include "cpu/backend/x64/x64_backend.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace x64 {

uint32_t CallBlock(RuntimeBlock *block, emu::Memory *memory, void *guest_ctx);
void DumpBlock(RuntimeBlock *block);
}
}
}
}

#endif
