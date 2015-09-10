#ifndef AICA_H
#define AICA_H

#include <stdint.h>
#include "hw/device.h"

namespace dreavm {
namespace emu {
class Dreamcast;
}

namespace hw {
namespace aica {

class AICA : public hw::Device {
 public:
  AICA(emu::Dreamcast *dc);

  uint32_t GetClockFrequency() { return 22579200; }

  void Init();
  uint32_t Execute(uint32_t cycles);

  uint32_t ReadRegister32(uint32_t addr);
  void WriteRegister32(uint32_t addr, uint32_t value);

  uint32_t ReadWave32(uint32_t addr);
  void WriteWave32(uint32_t addr, uint32_t value);

 private:
  emu::Dreamcast *dc_;
  uint8_t *aica_regs_;
  uint8_t *wave_ram_;
};
}
}
}

#endif
