#ifndef AICA_H
#define AICA_H

#include <stdint.h>

namespace re {
namespace hw {
struct Dreamcast;

namespace aica {

class AICA {
 public:
  AICA(hw::Dreamcast *dc);

  bool Init();

  // static uint32_t ReadRegister(void *ctx, uint32_t addr);
  // static void WriteRegister(void *ctx, uint32_t addr, uint32_t value);

  template <typename T>
  static T ReadWave(void *ctx, uint32_t addr);

  template <typename T>
  static void WriteWave(void *ctx, uint32_t addr, T value);

 private:
  hw::Dreamcast *dc_;
  // uint8_t *aica_regs_;
  uint8_t *wave_ram_;
};
}
}
}

#endif
