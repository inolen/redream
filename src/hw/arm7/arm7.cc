#include "hw/arm7/arm7.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

using namespace re::hw;
using namespace re::hw::arm7;

ARM7::ARM7(Dreamcast &dc) : Device(dc), ExecuteInterface(this), dc_(dc) {}

bool ARM7::Init() {
  ((void)dc_);
  return true;
}

void ARM7::Run(const std::chrono::nanoseconds &delta) {
}
