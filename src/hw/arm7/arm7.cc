#include "core/assert.h"
#include "hw/aica/aica.h"
#include "hw/arm7/arm7.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/scheduler.h"

using namespace re;
using namespace re::hw;
using namespace re::hw::aica;
using namespace re::hw::arm7;

// clang-format off
AM_BEGIN(ARM7, data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MASK(0x00ffffff) AM_DEVICE("aica", AICA, data_map)
  AM_RANGE(0x00800000, 0x00810fff) AM_MASK(0x00ffffff) AM_DEVICE("aica", AICA, reg_map)
AM_END();
// clang-format on

ARM7::ARM7(Dreamcast &dc)
    : Device(dc, "arm7"),
      ExecuteInterface(this),
      MemoryInterface(this, data_map),
      dc_(dc) {}

bool ARM7::Init() {
  (void)dc_;
  return true;
}

void ARM7::Run(const std::chrono::nanoseconds &delta) {
}
