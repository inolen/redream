#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

#include "core/arena.h"
#include "jit/frontend/frontend.h"

namespace re {
namespace jit {
namespace frontend {
namespace sh4 {

enum SH4BlockFlags {
  SH4_SLOWMEM = 0x1,
  SH4_DOUBLE_PR = 0x2,
  SH4_DOUBLE_SZ = 0x4,
  SH4_SINGLE_INSTR = 0x8,
};

class SH4Frontend : public Frontend {
 public:
  SH4Frontend();

  ir::IRBuilder &TranslateCode(uint32_t guest_addr, uint8_t *host_addr,
                               int flags);

 private:
  Arena arena_;
};
}
}
}
}

#endif
