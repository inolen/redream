#include "hw/holly/holly.h"
#include "hw/maple/maple.h"
#include "hw/maple/controller.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

using namespace re;
using namespace re::hw;
using namespace re::hw::holly;
using namespace re::hw::maple;
using namespace re::hw::sh4;
using namespace re::ui;

Maple::Maple(Dreamcast &dc)
    : Device(dc, "maple"),
      WindowInterface(this),
      dc_(dc),
      sh4_(nullptr),
      holly_(nullptr),
      devices_() {
  // default controller device
  devices_[0] = std::unique_ptr<Controller>(new Controller());
}

bool Maple::Init() {
  sh4_ = dc_.sh4();
  holly_ = dc_.holly();

// initialize registers
#define MAPLE_REG_R32(name) \
  holly_->reg(name##_OFFSET).read = make_delegate(&Maple::name##_r, this)
#define MAPLE_REG_W32(name) \
  holly_->reg(name##_OFFSET).write = make_delegate(&Maple::name##_w, this)
  MAPLE_REG_W32(SB_MDST);
#undef MAPLE_REG_R32
#undef MAPLE_REG_W32

  return true;
}

// The controller can be started up by two methods: by software, or by hardware
// in synchronization with the V-BLANK signal. These methods are selected
// through the trigger selection register (SB_MDTSEL).
void Maple::VBlank() {
  uint32_t enabled = holly_->SB_MDEN;
  uint32_t vblank_initiate = holly_->SB_MDTSEL;

  if (enabled && vblank_initiate) {
    StartDMA();
  }

  // TODO maple vblank interrupt?
}

void Maple::OnKeyDown(Keycode key, int16_t value) {
  std::unique_ptr<MapleDevice> &dev = devices_[0];

  if (!dev) {
    return;
  }

  dev->HandleInput(key, value);
}

void Maple::StartDMA() {
  uint32_t start_addr = holly_->SB_MDSTAR;
  MapleTransferDesc desc;
  MapleFrame frame, res;

  do {
    desc.full = sh4_->space().R64(start_addr);
    start_addr += 8;

    // read input
    frame.header.full = sh4_->space().R32(start_addr);
    start_addr += 4;

    for (uint32_t i = 0; i < frame.header.num_words; i++) {
      frame.params[i] = sh4_->space().R32(start_addr);
      start_addr += 4;
    }

    // handle frame and write response
    std::unique_ptr<MapleDevice> &dev = devices_[desc.port];

    if (dev && dev->HandleFrame(frame, res)) {
      sh4_->space().W32(desc.result_addr, res.header.full);
      desc.result_addr += 4;

      for (uint32_t i = 0; i < res.header.num_words; i++) {
        sh4_->space().W32(desc.result_addr, res.params[i]);
        desc.result_addr += 4;
      }
    } else {
      sh4_->space().W32(desc.result_addr, 0xffffffff);
    }
  } while (!desc.last);

  holly_->SB_MDST = 0;
  holly_->RequestInterrupt(HOLLY_INTC_MDEINT);
}

W32_DELEGATE(Maple::SB_MDST) {
  uint32_t enabled = holly_->SB_MDEN;
  if (enabled) {
    if (reg.value) {
      StartDMA();
    }
  } else {
    reg.value = 0;
  }
}
