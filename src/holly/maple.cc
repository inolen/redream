#include "core/core.h"
#include "holly/holly.h"
#include "holly/maple.h"
#include "holly/maple_controller.h"

using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::emu;
using namespace dreavm::holly;
using namespace dreavm::system;

#define SB_REG(name) holly_.SB_##name

Maple::Maple(Memory &memory, SH4 &sh4, Holly &holly)
    : memory_(memory), /*sh4_(sh4),*/ holly_(holly), devices_() {
  // default controller device
  devices_[0] = std::unique_ptr<MapleController>(new MapleController());
}

bool Maple::Init() { return true; }

bool Maple::HandleInput(int port, Keycode key, int16_t value) {
  CHECK_LT(port, MAX_PORTS);
  std::unique_ptr<MapleDevice> &dev = devices_[port];
  if (!dev) {
    return false;
  }
  return dev->HandleInput(key, value);
}

// The controller can be started up by two methods: by software, or by hardware
// in synchronization with the V-BLANK signal. These methods are selected
// through the trigger selection register (SB_MDTSEL).
void Maple::VBlank() {
  uint32_t enabled = SB_REG(MDEN);
  uint32_t vblank_initiate = SB_REG(MDTSEL);

  if (enabled && vblank_initiate) {
    StartDMA();
  }

  // TODO maple vblank interrupt?
}

uint32_t Maple::ReadRegister(Register &reg, uint32_t addr) { return reg.value; }

void Maple::WriteRegister(Register &reg, uint32_t addr, uint32_t value) {
  // T old = reg.value;
  reg.value = value;

  if (addr == SB_MDST_OFFSET) {
    uint32_t enabled = SB_REG(MDEN);
    if (enabled) {
      if (value) {
        StartDMA();
      }
    } else {
      reg.value = 0;
    }
  }
}

void Maple::StartDMA() {
  uint32_t start_addr = SB_REG(MDSTAR);
  MapleTransferDesc desc;
  MapleFrame frame, res;

  do {
    desc.full = memory_.R64(start_addr);
    start_addr += 8;

    // read input
    frame.header.full = memory_.R32(start_addr);
    start_addr += 4;

    for (int i = 0; i < frame.header.num_words; i++) {
      frame.params[i] = memory_.R32(start_addr);
      start_addr += 4;
    }

    // handle frame and write response
    CHECK_LT(desc.port, MAX_PORTS);
    std::unique_ptr<MapleDevice> &dev = devices_[desc.port];

    if (dev && dev->HandleFrame(frame, res)) {
      memory_.W32(desc.result_addr, res.header.full);
      desc.result_addr += 4;

      for (int i = 0; i < res.header.num_words; i++) {
        memory_.W32(desc.result_addr, res.params[i]);
        desc.result_addr += 4;
      }
    } else {
      memory_.W32(desc.result_addr, 0xffffffff);
    }
  } while (!desc.last);

  SB_REG(MDST) = 0;
  holly_.RequestInterrupt(HOLLY_INTC_MDEINT);
}
