#ifndef PVR2_H
#define PVR2_H

#include <stdint.h>
#include "hw/holly/pvr2_types.h"
#include "hw/machine.h"
#include "hw/memory.h"
#include "hw/register.h"
#include "hw/scheduler.h"

namespace re {
namespace hw {

class Dreamcast;

namespace holly {

class Holly;
class TileAccelerator;

class PVR2 : public Device {
 public:
  AM_DECLARE(reg_map);
  AM_DECLARE(vram_map);

  PVR2(Dreamcast &dc);

  Register &reg(int offset) { return regs_[offset]; }

  bool Init() final;

#define PVR_REG(offset, name, flags, default, type) \
  type &name = reinterpret_cast<type &>(regs_[name##_OFFSET].value);
#include "hw/holly/pvr2_regs.inc"
#undef PVR_REG

 private:
  uint32_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint32_t value);
  template <typename T>
  T ReadVRamInterleaved(uint32_t addr);
  template <typename T>
  void WriteVRamInterleaved(uint32_t addr, T value);

  void ReconfigureSPG();
  void NextScanline();

  DECLARE_W32_DELEGATE(SPG_LOAD);
  DECLARE_W32_DELEGATE(FB_R_CTRL);

  Dreamcast &dc_;
  Scheduler *scheduler_;
  holly::Holly *holly_;
  holly::TileAccelerator *ta_;
  uint8_t *palette_ram_;
  uint8_t *video_ram_;

  Register regs_[NUM_PVR_REGS];

  TimerHandle line_timer_;
  int line_clock_;
  uint32_t current_scanline_;
};
}
}
}

#endif
