#ifndef HOLLY_H
#define HOLLY_H

#include <stdint.h>
#include "core/delegate.h"
#include "hw/holly/holly_types.h"
#include "hw/machine.h"
#include "hw/memory.h"
#include "hw/register.h"

namespace re {
namespace hw {
namespace gdrom {
class GDROM;
}
namespace maple {
class Maple;
}
namespace sh4 {
class SH4;
}

class Dreamcast;

namespace holly {

class Holly : public Device {
 public:
  AM_DECLARE(reg_map);

  Holly(Dreamcast &dc);

  Register &reg(int offset) { return regs_[offset]; }

  bool Init() final;

  void RequestInterrupt(HollyInterrupt intr);
  void UnrequestInterrupt(HollyInterrupt intr);

#define HOLLY_REG(offset, name, flags, default, type) \
  type &name = reinterpret_cast<type &>(regs_[name##_OFFSET].value);
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG

 private:
  template <typename T>
  T ReadRegister(uint32_t addr);
  template <typename T>
  void WriteRegister(uint32_t addr, T value);

  void UpdateSH4Interrupts();

  DECLARE_R32_DELEGATE(SB_ISTNRM);
  DECLARE_W32_DELEGATE(SB_ISTNRM);
  DECLARE_W32_DELEGATE(SB_ISTEXT);
  DECLARE_W32_DELEGATE(SB_ISTERR);
  DECLARE_W32_DELEGATE(SB_IML2NRM);
  DECLARE_W32_DELEGATE(SB_IML2EXT);
  DECLARE_W32_DELEGATE(SB_IML2ERR);
  DECLARE_W32_DELEGATE(SB_IML4NRM);
  DECLARE_W32_DELEGATE(SB_IML4EXT);
  DECLARE_W32_DELEGATE(SB_IML4ERR);
  DECLARE_W32_DELEGATE(SB_IML6NRM);
  DECLARE_W32_DELEGATE(SB_IML6EXT);
  DECLARE_W32_DELEGATE(SB_IML6ERR);
  DECLARE_W32_DELEGATE(SB_C2DST);
  DECLARE_W32_DELEGATE(SB_SDST);
  DECLARE_W32_DELEGATE(SB_GDST);
  DECLARE_W32_DELEGATE(SB_ADEN);
  DECLARE_W32_DELEGATE(SB_ADST);
  DECLARE_W32_DELEGATE(SB_E1EN);
  DECLARE_W32_DELEGATE(SB_E1ST);
  DECLARE_W32_DELEGATE(SB_E2EN);
  DECLARE_W32_DELEGATE(SB_E2ST);
  DECLARE_W32_DELEGATE(SB_DDEN);
  DECLARE_W32_DELEGATE(SB_DDST);
  DECLARE_W32_DELEGATE(SB_PDEN);
  DECLARE_W32_DELEGATE(SB_PDST);

  Dreamcast &dc_;
  gdrom::GDROM *gdrom_;
  maple::Maple *maple_;
  sh4::SH4 *sh4_;

  Register regs_[NUM_HOLLY_REGS];
};
}
}
}

#endif
