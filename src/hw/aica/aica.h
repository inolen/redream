#ifndef AICA_H
#define AICA_H

#include <stdint.h>
#include "hw/device.h"

namespace dreavm {
namespace hw {
class Dreamcast;

namespace aica {

class AICA : public hw::Device {
 public:
  AICA(hw::Dreamcast *dc);

  int GetClockFrequency() { return 22579200; }

  bool Init();
  int Execute(int cycles);

  static uint32_t ReadRegister(void *ctx, uint32_t addr);
  static void WriteRegister(void *ctx, uint32_t addr, uint32_t value);

  static uint32_t ReadWave(void *ctx, uint32_t addr);
  static void WriteWave(void *ctx, uint32_t addr, uint32_t value);

 private:
  hw::Dreamcast *dc_;
  uint8_t *aica_regs_;
  uint8_t *wave_ram_;
};
}
}
}

#endif
