#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

#include "jit/frontend/frontend.h"

namespace dvm {
namespace jit {
namespace frontend {
namespace sh4 {

class SH4Frontend : public Frontend {
 public:
  SH4Frontend(hw::Memory &memory);

  std::unique_ptr<ir::IRBuilder> BuildBlock(uint32_t addr,
                                            const void *guest_ctx);
};
}
}
}
}

#endif
