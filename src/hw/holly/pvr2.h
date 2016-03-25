#ifndef PVR2_H
#define PVR2_H

#include <stdint.h>
#include "hw/holly/pvr2_types.h"
#include "hw/machine.h"
#include "hw/scheduler.h"

namespace re {
namespace hw {

class Dreamcast;
struct Register;

namespace holly {

class Holly;
class TileAccelerator;

#define PVR2_DECLARE_R32_DELEGATE(name) uint32_t name##_read(Register &)
#define PVR2_DECLARE_W32_DELEGATE(name) void name##_write(Register &, uint32_t)

#define PVR2_REGISTER_R32_DELEGATE(name) \
  regs_[name##_OFFSET].read = make_delegate(&PVR2::name##_read, this)
#define PVR2_REGISTER_W32_DELEGATE(name) \
  regs_[name##_OFFSET].write = make_delegate(&PVR2::name##_write, this)

#define PVR2_R32_DELEGATE(name) uint32_t PVR2::name##_read(Register &reg)
#define PVR2_W32_DELEGATE(name) \
  void PVR2::name##_write(Register &reg, uint32_t old_value)

class PVR2 : public Device, public MemoryInterface {
 public:
  PVR2(Dreamcast &dc);

  Register &reg(int offset) { return regs_[offset]; }

  bool Init() final;

#define PVR_REG(offset, name, flags, default, type) \
  type &name = reinterpret_cast<type &>(regs_[name##_OFFSET].value);
#include "hw/holly/pvr2_regs.inc"
#undef PVR_REG

 private:
  // MemoryInterface
  void MapPhysicalMemory(Memory &memory, MemoryMap &memmap) final;
  uint32_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint32_t value);
  template <typename T>
  T ReadVRamInterleaved(uint32_t addr);
  template <typename T>
  void WriteVRamInterleaved(uint32_t addr, T value);

  void ReconfigureSPG();
  void NextScanline();

  PVR2_DECLARE_W32_DELEGATE(SPG_LOAD);
  PVR2_DECLARE_W32_DELEGATE(FB_R_CTRL);

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
