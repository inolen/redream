#ifndef INTERPRETER_BLOCK_H
#define INTERPRETER_BLOCK_H

#include "cpu/backend/interpreter/interpreter_backend.h"

namespace dreavm {
namespace cpu {
namespace backend {
namespace interpreter {

uint32_t CallBlock(RuntimeBlock *block, emu::Memory *memory, void *guest_ctx);
void DumpBlock(RuntimeBlock *block);
}
}
}
}

#endif
