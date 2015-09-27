#include "core/core.h"
#include "hw/maple/maple_controller.h"
#include "hw/dreamcast.h"

using namespace dreavm;
using namespace dreavm::hw;
using namespace dreavm::hw::holly;
using namespace dreavm::hw::maple;
using namespace dreavm::hw::sh4;
using namespace dreavm::sys;

Maple::Maple(Dreamcast *dc) : dc_(dc), devices_() {
  // default controller device
  devices_[0] = std::unique_ptr<MapleController>(new MapleController());
}

bool Maple::Init() {
  memory_ = dc_->memory();
  holly_ = dc_->holly();
  holly_regs_ = dc_->holly_regs();

  return true;
}

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
  uint32_t enabled = dc_->SB_MDEN;
  uint32_t vblank_initiate = dc_->SB_MDTSEL;

  if (enabled && vblank_initiate) {
    StartDMA();
  }

  // TODO maple vblank interrupt?
}

uint32_t Maple::ReadRegister(void *ctx, uint32_t addr) {
  Maple *self = reinterpret_cast<Maple *>(ctx);

  uint32_t offset = (0xc00 + addr) >> 2;
  Register &reg = self->holly_regs_[offset];

  if (!(reg.flags & R)) {
    LOG_WARNING("Invalid read access at 0x%x", addr);
    return 0;
  }

  return reg.value;
}

void Maple::WriteRegister(void *ctx, uint32_t addr, uint32_t value) {
  Maple *self = reinterpret_cast<Maple *>(ctx);

  uint32_t offset = (0xc00 + addr) >> 2;
  Register &reg = self->holly_regs_[offset];

  if (!(reg.flags & W)) {
    LOG_WARNING("Invalid write access at 0x%x", addr);
    return;
  }

  // uint32_t old = reg.value;
  reg.value = value;

  switch (offset) {
    case SB_MDST_OFFSET: {
      uint32_t enabled = self->dc_->SB_MDEN;
      if (enabled) {
        if (value) {
          self->StartDMA();
        }
      } else {
        reg.value = 0;
      }
    } break;
  }
}

void Maple::StartDMA() {
  uint32_t start_addr = dc_->SB_MDSTAR;
  MapleTransferDesc desc;
  MapleFrame frame, res;

  do {
    desc.full = memory_->R64(start_addr);
    start_addr += 8;

    // read input
    frame.header.full = memory_->R32(start_addr);
    start_addr += 4;

    for (uint32_t i = 0; i < frame.header.num_words; i++) {
      frame.params[i] = memory_->R32(start_addr);
      start_addr += 4;
    }

    // handle frame and write response
    std::unique_ptr<MapleDevice> &dev = devices_[desc.port];

    if (dev && dev->HandleFrame(frame, res)) {
      memory_->W32(desc.result_addr, res.header.full);
      desc.result_addr += 4;

      for (uint32_t i = 0; i < res.header.num_words; i++) {
        memory_->W32(desc.result_addr, res.params[i]);
        desc.result_addr += 4;
      }
    } else {
      memory_->W32(desc.result_addr, 0xffffffff);
    }
  } while (!desc.last);

  dc_->SB_MDST = 0;
  holly_->RequestInterrupt(HOLLY_INTC_MDEINT);
}
