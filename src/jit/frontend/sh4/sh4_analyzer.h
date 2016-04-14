#ifndef SH4_ANALYZER_H
#define SH4_ANALYZER_H

#include <stddef.h>
#include <stdint.h>

namespace re {
namespace jit {
namespace frontend {
namespace sh4 {

class SH4Analyzer {
 public:
  static void AnalyzeBlock(uint32_t guest_addr, uint8_t *guest_ptr, int flags,
                           int *size);
};
}
}
}
}

#endif
