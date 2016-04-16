#ifndef AICA_H
#define AICA_H

#include <stdint.h>
#include "hw/aica/aica_types.h"
#include "hw/machine.h"

namespace re {
namespace hw {
class Dreamcast;

namespace arm7 {
class ARM7;
}

namespace sh4 {
class SH4;
}

namespace aica {

class AICA : public Device, public ExecuteInterface, public MemoryInterface {
 public:
  AICA(Dreamcast &dc);

  bool Init() final;

 private:
  // ExecuteInterface
  void Run(const std::chrono::nanoseconds &delta) final;

  // MemoryInterface
  void MapPhysicalMemory(Memory &memory, MemoryMap &memmap) final;

  template <typename T>
  T ReadRegister(uint32_t addr);
  template <typename T>
  void WriteRegister(uint32_t addr, T value);

  template <typename T>
  T ReadWave(uint32_t addr);
  template <typename T>
  void WriteWave(uint32_t addr, T value);

  void UpdateARMInterrupts();
  void UpdateSH4Interrupts();

  Dreamcast &dc_;
  sh4::SH4 *sh4_;
  arm7::ARM7 *arm7_;
  uint8_t *aica_regs_;
  uint8_t *wave_ram_;
  CommonData *common_data_;
};
}
}
}

#endif
