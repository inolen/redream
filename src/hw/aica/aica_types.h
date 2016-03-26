#ifndef AICA_TYPES_H
#define AICA_TYPES_H

#include "hw/regions.h"

namespace re {
namespace hw {
namespace aica {

enum {
#define AICA_REG(addr, name, flags, default, type) \
  name##_OFFSET = addr - AICA_REG_START,
#include "hw/aica/aica_regs.inc"
#undef AICA_REG
  NUM_AICA_REGS = AICA_REG_SIZE >> 2,
};
}
}
]

#endif
