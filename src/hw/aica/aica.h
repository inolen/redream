#ifndef AICA_H
#define AICA_H

#include <stdint.h>
#include "hw/machine.h"

namespace re {
namespace hw {
class Dreamcast;

namespace aica {

class AICA : public Device, public MemoryInterface {
 public:
  AICA(Dreamcast *dc);

  bool Init() final;

 protected:
  void MapPhysicalMemory(Memory &memory, MemoryMap &memmap) final;

 private:
  uint32_t ReadRegister(uint32_t addr);
  void WriteRegister(uint32_t addr, uint32_t value);

  template <typename T>
  T ReadWave(uint32_t addr);
  template <typename T>
  void WriteWave(uint32_t addr, T value);

  Dreamcast *dc_;
  uint8_t *aica_regs_;
  uint8_t *wave_ram_;
};
}
}
}

#endif
