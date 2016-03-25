#ifndef HOLLY_H
#define HOLLY_H

#include <stdint.h>
#include "core/delegate.h"
#include "hw/holly/holly_types.h"
#include "hw/machine.h"
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

#define HOLLY_DECLARE_R32_DELEGATE(name) uint32_t name##_read(Register &)
#define HOLLY_DECLARE_W32_DELEGATE(name) void name##_write(Register &, uint32_t)

#define HOLLY_REGISTER_R32_DELEGATE(name) \
  regs_[name##_OFFSET].read = make_delegate(&Holly::name##_read, this)
#define HOLLY_REGISTER_W32_DELEGATE(name) \
  regs_[name##_OFFSET].write = make_delegate(&Holly::name##_write, this)

#define HOLLY_R32_DELEGATE(name) uint32_t Holly::name##_read(Register &reg)
#define HOLLY_W32_DELEGATE(name) \
  void Holly::name##_write(Register &reg, uint32_t old_value)

class Holly : public Device, public MemoryInterface {
 public:
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
  // MemoryInterface
  void MapPhysicalMemory(Memory &memory, MemoryMap &memmap) final;
  template <typename T>
  T ReadRegister(uint32_t addr);
  template <typename T>
  void WriteRegister(uint32_t addr, T value);

  void ForwardRequestInterrupts();

  HOLLY_DECLARE_R32_DELEGATE(SB_ISTNRM);
  HOLLY_DECLARE_W32_DELEGATE(SB_ISTNRM);
  HOLLY_DECLARE_W32_DELEGATE(SB_ISTEXT);
  HOLLY_DECLARE_W32_DELEGATE(SB_ISTERR);
  HOLLY_DECLARE_W32_DELEGATE(SB_IML2NRM);
  HOLLY_DECLARE_W32_DELEGATE(SB_IML2EXT);
  HOLLY_DECLARE_W32_DELEGATE(SB_IML2ERR);
  HOLLY_DECLARE_W32_DELEGATE(SB_IML4NRM);
  HOLLY_DECLARE_W32_DELEGATE(SB_IML4EXT);
  HOLLY_DECLARE_W32_DELEGATE(SB_IML4ERR);
  HOLLY_DECLARE_W32_DELEGATE(SB_IML6NRM);
  HOLLY_DECLARE_W32_DELEGATE(SB_IML6EXT);
  HOLLY_DECLARE_W32_DELEGATE(SB_IML6ERR);
  HOLLY_DECLARE_W32_DELEGATE(SB_C2DST);
  HOLLY_DECLARE_W32_DELEGATE(SB_SDST);
  HOLLY_DECLARE_W32_DELEGATE(SB_GDST);
  HOLLY_DECLARE_W32_DELEGATE(SB_ADEN);
  HOLLY_DECLARE_W32_DELEGATE(SB_ADST);
  HOLLY_DECLARE_W32_DELEGATE(SB_E1EN);
  HOLLY_DECLARE_W32_DELEGATE(SB_E1ST);
  HOLLY_DECLARE_W32_DELEGATE(SB_E2EN);
  HOLLY_DECLARE_W32_DELEGATE(SB_E2ST);
  HOLLY_DECLARE_W32_DELEGATE(SB_DDEN);
  HOLLY_DECLARE_W32_DELEGATE(SB_DDST);
  HOLLY_DECLARE_W32_DELEGATE(SB_PDEN);
  HOLLY_DECLARE_W32_DELEGATE(SB_PDST);

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
