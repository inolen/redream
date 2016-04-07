#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

#include "core/arena.h"
#include "jit/frontend/frontend.h"

namespace re {
namespace jit {
namespace frontend {
namespace sh4 {

class SH4Frontend : public Frontend {
 public:
  SH4Frontend(hw::Memory &memory, void *guest_ctx);

  ir::IRBuilder &BuildBlock(uint32_t addr, int max_instrs);

 private:
  Arena arena_;
};
}
}
}
}

#endif
