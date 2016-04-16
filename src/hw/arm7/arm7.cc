#include "core/assert.h"
#include "hw/arm7/arm7.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/scheduler.h"

using namespace re;
using namespace re::hw;
using namespace re::hw::arm7;

enum {
  ARM7_CLOCK_FREQ = 22579200,
};

ARM7::ARM7(Dreamcast &dc) : Device(dc), ExecuteInterface(this), dc_(dc) {}

bool ARM7::Init() {
  memory_ = dc_.memory;

  return true;
}

void ARM7::Run(const std::chrono::nanoseconds &delta) {
  // ctx_.num_cycles = NANO_TO_CYCLES(delta, ARM7_CLOCK_FREQ);

  // while (ctx_.num_cycles > 0) {
  // }
}
