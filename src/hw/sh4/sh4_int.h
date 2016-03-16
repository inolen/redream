#ifndef SH4_INT_H
#define SH4_INT_H

namespace re {
namespace hw {
namespace sh4 {

struct InterruptInfo {
  int intevt, default_priority, ipr, ipr_shift;
};

enum Interrupt {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) SH4_INTC_##name,
#include "hw/sh4/sh4_int.inc"
#undef SH4_INT
  NUM_INTERRUPTS
};

}
}
}

#endif
