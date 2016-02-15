#ifndef AICA_H
#define AICA_H

#include <stdint.h>

namespace re {
namespace hw {
struct Dreamcast;

extern bool MapMemory(Dreamcast &dc);

namespace aica {

class AICA {
  friend bool re::hw::MapMemory(Dreamcast &dc);

 public:
  AICA(Dreamcast *dc);

  bool Init();

 private:
  // static uint32_t ReadRegister(void *ctx, uint32_t addr);
  // static void WriteRegister(void *ctx, uint32_t addr, uint32_t value);

  template <typename T>
  T ReadWave(uint32_t addr);

  template <typename T>
  void WriteWave(uint32_t addr, T value);

  Dreamcast *dc_;
  // uint8_t *aica_regs_;
  uint8_t *wave_ram_;
};
}
}
}

#endif
