#ifndef SH4_FRONTEND_H
#define SH4_FRONTEND_H

#include "cpu/frontend/frontend.h"
#include "cpu/frontend/sh4/sh4_builder.h"
#include "cpu/frontend/sh4/sh4_instr.h"
#include "cpu/sh4.h"

namespace dreavm {
namespace cpu {
namespace frontend {
namespace sh4 {

class SH4Frontend : public Frontend {
 public:
  SH4Frontend(emu::Memory &memory);

  std::unique_ptr<ir::IRBuilder> BuildBlock(uint32_t addr,
                                            const void *guest_ctx);
};
}
}
}
}

#endif
