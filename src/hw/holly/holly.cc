#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/maple/maple.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

using namespace re;
using namespace re::hw;
using namespace re::hw::gdrom;
using namespace re::hw::holly;
using namespace re::hw::maple;
using namespace re::hw::sh4;
using namespace re::sys;

// clang-format off
AM_BEGIN(Holly, reg_map)
  AM_RANGE(0x00000000, 0x00001fff) AM_HANDLE(&Holly::ReadRegister<uint8_t>,
                                             &Holly::ReadRegister<uint16_t>,
                                             &Holly::ReadRegister<uint32_t>,
                                             nullptr,
                                             &Holly::WriteRegister<uint8_t>,
                                             &Holly::WriteRegister<uint16_t>,
                                             &Holly::WriteRegister<uint32_t>,
                                             nullptr)
AM_END()
    // clang-format on

    Holly::Holly(Dreamcast &dc)
    : Device(dc, "holly"),
      dc_(dc),
      gdrom_(nullptr),
      maple_(nullptr),
      sh4_(nullptr),
      regs_() {}

bool Holly::Init() {
  gdrom_ = dc_.gdrom();
  maple_ = dc_.maple();
  sh4_ = dc_.sh4();

// initialize registers
#define HOLLY_REG(addr, name, flags, default, type) \
  regs_[name##_OFFSET] = {flags, default};
#define HOLLY_REG_R32(name) \
  regs_[name##_OFFSET].read = make_delegate(&Holly::name##_r, this)
#define HOLLY_REG_W32(name) \
  regs_[name##_OFFSET].write = make_delegate(&Holly::name##_w, this)
#include "hw/holly/holly_regs.inc"
  HOLLY_REG_R32(SB_ISTNRM);
  HOLLY_REG_W32(SB_ISTNRM);
  HOLLY_REG_W32(SB_ISTEXT);
  HOLLY_REG_W32(SB_ISTERR);
  HOLLY_REG_W32(SB_IML2NRM);
  HOLLY_REG_W32(SB_IML2EXT);
  HOLLY_REG_W32(SB_IML2ERR);
  HOLLY_REG_W32(SB_IML4NRM);
  HOLLY_REG_W32(SB_IML4EXT);
  HOLLY_REG_W32(SB_IML4ERR);
  HOLLY_REG_W32(SB_IML6NRM);
  HOLLY_REG_W32(SB_IML6EXT);
  HOLLY_REG_W32(SB_IML6ERR);
  HOLLY_REG_W32(SB_C2DST);
  HOLLY_REG_W32(SB_SDST);
  HOLLY_REG_W32(SB_GDST);
  HOLLY_REG_W32(SB_ADEN);
  HOLLY_REG_W32(SB_ADST);
  HOLLY_REG_W32(SB_E1EN);
  HOLLY_REG_W32(SB_E1ST);
  HOLLY_REG_W32(SB_E2EN);
  HOLLY_REG_W32(SB_E2ST);
  HOLLY_REG_W32(SB_DDEN);
  HOLLY_REG_W32(SB_DDST);
  HOLLY_REG_W32(SB_PDEN);
  HOLLY_REG_W32(SB_PDST);
#undef HOLLY_REG
#undef HOLLY_REG_R32
#undef HOLLY_REG_W32

  return true;
}

void Holly::RequestInterrupt(HollyInterrupt intr) {
  HollyInterruptType type =
      static_cast<HollyInterruptType>(intr & HOLLY_INTC_MASK);
  uint32_t irq = static_cast<uint32_t>(intr & ~HOLLY_INTC_MASK);

  if (intr == HOLLY_INTC_PCVOINT) {
    maple_->VBlank();
  }

  switch (type) {
    case HOLLY_INTC_NRM:
      SB_ISTNRM |= irq;
      break;

    case HOLLY_INTC_EXT:
      SB_ISTEXT |= irq;
      break;

    case HOLLY_INTC_ERR:
      SB_ISTERR |= irq;
      break;
  }

  UpdateSH4Interrupts();
}

void Holly::UnrequestInterrupt(HollyInterrupt intr) {
  HollyInterruptType type =
      static_cast<HollyInterruptType>(intr & HOLLY_INTC_MASK);
  uint32_t irq = static_cast<uint32_t>(intr & ~HOLLY_INTC_MASK);

  switch (type) {
    case HOLLY_INTC_NRM:
      SB_ISTNRM &= ~irq;
      break;

    case HOLLY_INTC_EXT:
      SB_ISTEXT &= ~irq;
      break;

    case HOLLY_INTC_ERR:
      SB_ISTERR &= ~irq;
      break;
  }

  UpdateSH4Interrupts();
}

template <typename T>
T Holly::ReadRegister(uint32_t addr) {
  uint32_t offset = addr >> 2;
  Register &reg = regs_[offset];

  if (!(reg.flags & R)) {
    LOG_WARNING("Invalid read access at 0x%x", addr);
    return 0;
  }

  if (reg.read) {
    return static_cast<T>(reg.read(reg));
  }

  return static_cast<T>(reg.value);
}

template <typename T>
void Holly::WriteRegister(uint32_t addr, T value) {
  uint32_t offset = addr >> 2;
  Register &reg = regs_[offset];

  if (!(reg.flags & W)) {
    LOG_WARNING("Invalid write access at 0x%x", addr);
    return;
  }

  uint32_t old_value = reg.value;
  reg.value = static_cast<uint32_t>(value);

  if (reg.write) {
    reg.write(reg, old_value);
  }
}

void Holly::UpdateSH4Interrupts() {
  // trigger the respective level-encoded interrupt on the sh4 interrupt
  // controller
  {
    if ((SB_ISTNRM & SB_IML6NRM) || (SB_ISTERR & SB_IML6ERR) ||
        (SB_ISTEXT & SB_IML6EXT)) {
      sh4_->RequestInterrupt(SH4_INTC_IRL_9);
    } else {
      sh4_->UnrequestInterrupt(SH4_INTC_IRL_9);
    }
  }

  {
    if ((SB_ISTNRM & SB_IML4NRM) || (SB_ISTERR & SB_IML4ERR) ||
        (SB_ISTEXT & SB_IML4EXT)) {
      sh4_->RequestInterrupt(SH4_INTC_IRL_11);
    } else {
      sh4_->UnrequestInterrupt(SH4_INTC_IRL_11);
    }
  }

  {
    if ((SB_ISTNRM & SB_IML2NRM) || (SB_ISTERR & SB_IML2ERR) ||
        (SB_ISTEXT & SB_IML2EXT)) {
      sh4_->RequestInterrupt(SH4_INTC_IRL_13);
    } else {
      sh4_->UnrequestInterrupt(SH4_INTC_IRL_13);
    }
  }
}

R32_DELEGATE(Holly::SB_ISTNRM) {
  // Note that the two highest bits indicate the OR'ed result of all of the
  // bits in SB_ISTEXT and SB_ISTERR, respectively, and writes to these two
  // bits are ignored.
  uint32_t v = reg.value & 0x3fffffff;
  if (SB_ISTEXT) {
    v |= 0x40000000;
  }
  if (SB_ISTERR) {
    v |= 0x80000000;
  }
  return v;
}

W32_DELEGATE(Holly::SB_ISTNRM) {
  // writing a 1 clears the interrupt
  reg.value = old_value & ~reg.value;
  UpdateSH4Interrupts();
}

W32_DELEGATE(Holly::SB_ISTEXT) {
  reg.value = old_value & ~reg.value;
  UpdateSH4Interrupts();
}

W32_DELEGATE(Holly::SB_ISTERR) {
  reg.value = old_value & ~reg.value;
  UpdateSH4Interrupts();
}

W32_DELEGATE(Holly::SB_IML2NRM) { UpdateSH4Interrupts(); }

W32_DELEGATE(Holly::SB_IML2EXT) { UpdateSH4Interrupts(); }

W32_DELEGATE(Holly::SB_IML2ERR) { UpdateSH4Interrupts(); }

W32_DELEGATE(Holly::SB_IML4NRM) { UpdateSH4Interrupts(); }

W32_DELEGATE(Holly::SB_IML4EXT) { UpdateSH4Interrupts(); }

W32_DELEGATE(Holly::SB_IML4ERR) { UpdateSH4Interrupts(); }

W32_DELEGATE(Holly::SB_IML6NRM) { UpdateSH4Interrupts(); }

W32_DELEGATE(Holly::SB_IML6EXT) { UpdateSH4Interrupts(); }

W32_DELEGATE(Holly::SB_IML6ERR) { UpdateSH4Interrupts(); }

W32_DELEGATE(Holly::SB_C2DST) {
  if (!reg.value) {
    return;
  }

  // FIXME what are SB_LMMODE0 / SB_LMMODE1
  DTR dtr;
  dtr.channel = 2;
  dtr.rw = false;
  dtr.addr = SB_C2DSTAT;
  sh4_->DDT(dtr);

  SB_C2DLEN = 0;
  SB_C2DST = 0;
  RequestInterrupt(HOLLY_INTC_DTDE2INT);
}

W32_DELEGATE(Holly::SB_SDST) {
  if (!reg.value) {
    return;
  }

  LOG_FATAL("Sort DMA not supported");
}

W32_DELEGATE(Holly::SB_GDST) {
  // if a "0" is written to this register, it is ignored
  reg.value |= old_value;

  if (!reg.value) {
    return;
  }

  CHECK_EQ(SB_GDEN, 1);   // dma enabled
  CHECK_EQ(SB_GDDIR, 1);  // gd-rom -> system memory

  int transfer_size = SB_GDLEN;
  uint32_t start = SB_GDSTAR;

  int remaining = transfer_size;
  uint32_t addr = start;

  gdrom_->BeginDMA();

  while (remaining) {
    // read a single sector at a time from the gdrom
    uint8_t sector_data[SECTOR_SIZE];
    int n = gdrom_->ReadDMA(sector_data, sizeof(sector_data));

    DTR dtr;
    dtr.channel = 0;
    dtr.rw = true;
    dtr.data = sector_data;
    dtr.addr = addr;
    dtr.size = n;
    sh4_->DDT(dtr);

    remaining -= n;
    addr += n;
  }

  gdrom_->EndDMA();

  SB_GDSTARD = start + transfer_size;
  SB_GDLEND = transfer_size;
  SB_GDST = 0;
  RequestInterrupt(HOLLY_INTC_G1DEINT);
}

W32_DELEGATE(Holly::SB_ADEN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored aica DMA request");
}

W32_DELEGATE(Holly::SB_ADST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored aica DMA request");
}

W32_DELEGATE(Holly::SB_E1EN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored ext1 DMA request");
}

W32_DELEGATE(Holly::SB_E1ST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored ext1 DMA request");
}

W32_DELEGATE(Holly::SB_E2EN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored ext2 DMA request");
}

W32_DELEGATE(Holly::SB_E2ST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored ext2 DMA request");
}

W32_DELEGATE(Holly::SB_DDEN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored dev DMA request");
}

W32_DELEGATE(Holly::SB_DDST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored dev DMA request");
}

W32_DELEGATE(Holly::SB_PDEN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored pvr DMA request");
}

W32_DELEGATE(Holly::SB_PDST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored pvr DMA request");
}
