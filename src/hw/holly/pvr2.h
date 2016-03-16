#ifndef PVR_CLX2_H
#define PVR_CLX2_H

#include <stdint.h>
#include "hw/holly/pvr2_regs.h"
#include "hw/machine.h"
#include "hw/scheduler.h"

namespace re {
namespace hw {

class Dreamcast;
struct Register;

namespace holly {

class Holly;
class TileAccelerator;

class PVR2 : public Device, public MemoryInterface {
 public:
  PVR2(Dreamcast *dc);

  float rps() { return rps_; }

  bool Init() final;

 protected:
  void MapPhysicalMemory(Memory &memory, MemoryMap &memmap) final;

 private:
  uint32_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint32_t value);

  template <typename T>
  T ReadVRamInterleaved(uint32_t addr);
  template <typename T>
  void WriteVRamInterleaved(uint32_t addr, T value);

  void ReconfigureSPG();
  void NextScanline();

  Dreamcast *dc_;
  Scheduler *scheduler_;
  holly::Holly *holly_;
  holly::TileAccelerator *ta_;
  Register *pvr_regs_;
  uint8_t *palette_ram_;
  uint8_t *video_ram_;

  TimerHandle line_timer_;
  int line_clock_;
  uint32_t current_scanline_;

  std::chrono::high_resolution_clock::time_point last_render_;
  float rps_;
};
}
}
}

#endif
