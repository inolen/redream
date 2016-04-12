#ifndef FRONTEND_H
#define FRONTEND_H

#include "jit/ir/ir_builder.h"

namespace re {
namespace jit {
namespace frontend {

class Frontend {
 public:
  virtual ~Frontend() {}

  virtual ir::IRBuilder &TranslateCode(uint32_t guest_addr, uint8_t *host_addr,
                                       int flags) = 0;
};
}
}
}

#endif
