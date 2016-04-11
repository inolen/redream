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

Holly::Holly(Dreamcast &dc)
    : Device(dc),
      MemoryInterface(this),
      dc_(dc),
      gdrom_(nullptr),
      maple_(nullptr),
      sh4_(nullptr),
      regs_() {}

bool Holly::Init() {
  gdrom_ = dc_.gdrom;
  maple_ = dc_.maple;
  sh4_ = dc_.sh4;

// initialize registers
#define HOLLY_REG(addr, name, flags, default, type) \
  regs_[name##_OFFSET] = {flags, default};
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG

  HOLLY_REGISTER_R32_DELEGATE(SB_ISTNRM);
  HOLLY_REGISTER_W32_DELEGATE(SB_ISTNRM);
  HOLLY_REGISTER_W32_DELEGATE(SB_ISTEXT);
  HOLLY_REGISTER_W32_DELEGATE(SB_ISTERR);
  HOLLY_REGISTER_W32_DELEGATE(SB_IML2NRM);
  HOLLY_REGISTER_W32_DELEGATE(SB_IML2EXT);
  HOLLY_REGISTER_W32_DELEGATE(SB_IML2ERR);
  HOLLY_REGISTER_W32_DELEGATE(SB_IML4NRM);
  HOLLY_REGISTER_W32_DELEGATE(SB_IML4EXT);
  HOLLY_REGISTER_W32_DELEGATE(SB_IML4ERR);
  HOLLY_REGISTER_W32_DELEGATE(SB_IML6NRM);
  HOLLY_REGISTER_W32_DELEGATE(SB_IML6EXT);
  HOLLY_REGISTER_W32_DELEGATE(SB_IML6ERR);
  HOLLY_REGISTER_W32_DELEGATE(SB_C2DST);
  HOLLY_REGISTER_W32_DELEGATE(SB_SDST);
  HOLLY_REGISTER_W32_DELEGATE(SB_GDST);
  HOLLY_REGISTER_W32_DELEGATE(SB_ADEN);
  HOLLY_REGISTER_W32_DELEGATE(SB_ADST);
  HOLLY_REGISTER_W32_DELEGATE(SB_E1EN);
  HOLLY_REGISTER_W32_DELEGATE(SB_E1ST);
  HOLLY_REGISTER_W32_DELEGATE(SB_E2EN);
  HOLLY_REGISTER_W32_DELEGATE(SB_E2ST);
  HOLLY_REGISTER_W32_DELEGATE(SB_DDEN);
  HOLLY_REGISTER_W32_DELEGATE(SB_DDST);
  HOLLY_REGISTER_W32_DELEGATE(SB_PDEN);
  HOLLY_REGISTER_W32_DELEGATE(SB_PDST);

  return true;
}

void Holly::RequestInterrupt(HollyInterrupt intr) {
  HollyInterruptType type =
      static_cast<HollyInterruptType>(intr & HOLLY_INTC_MASK);
  uint32_t irq = static_cast<uint32_t>(intr & ~HOLLY_INTC_MASK);

  if (intr == HOLLY_INTC_PCVOINT) {
    dc_.maple->VBlank();
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

  ForwardRequestInterrupts();
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

  ForwardRequestInterrupts();
}

void Holly::MapPhysicalMemory(Memory &memory, MemoryMap &memmap) {
  RegionHandle holly_handle = memory.AllocRegion(
      HOLLY_REG_BEGIN, HOLLY_REG_SIZE,
      make_delegate(&Holly::ReadRegister<uint8_t>, this),
      make_delegate(&Holly::ReadRegister<uint16_t>, this),
      make_delegate(&Holly::ReadRegister<uint32_t>, this), nullptr,
      make_delegate(&Holly::WriteRegister<uint8_t>, this),
      make_delegate(&Holly::WriteRegister<uint16_t>, this),
      make_delegate(&Holly::WriteRegister<uint32_t>, this), nullptr);

  memmap.Mount(holly_handle, HOLLY_REG_SIZE, HOLLY_REG_BEGIN);
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

void Holly::ForwardRequestInterrupts() {
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

HOLLY_R32_DELEGATE(SB_ISTNRM) {
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

HOLLY_W32_DELEGATE(SB_ISTNRM) {
  // writing a 1 clears the interrupt
  reg.value = old_value & ~reg.value;
  ForwardRequestInterrupts();
}

HOLLY_W32_DELEGATE(SB_ISTEXT) {
  reg.value = old_value & ~reg.value;
  ForwardRequestInterrupts();
}

HOLLY_W32_DELEGATE(SB_ISTERR) {
  reg.value = old_value & ~reg.value;
  ForwardRequestInterrupts();
}

HOLLY_W32_DELEGATE(SB_IML2NRM) { ForwardRequestInterrupts(); }

HOLLY_W32_DELEGATE(SB_IML2EXT) { ForwardRequestInterrupts(); }

HOLLY_W32_DELEGATE(SB_IML2ERR) { ForwardRequestInterrupts(); }

HOLLY_W32_DELEGATE(SB_IML4NRM) { ForwardRequestInterrupts(); }

HOLLY_W32_DELEGATE(SB_IML4EXT) { ForwardRequestInterrupts(); }

HOLLY_W32_DELEGATE(SB_IML4ERR) { ForwardRequestInterrupts(); }

HOLLY_W32_DELEGATE(SB_IML6NRM) { ForwardRequestInterrupts(); }

HOLLY_W32_DELEGATE(SB_IML6EXT) { ForwardRequestInterrupts(); }

HOLLY_W32_DELEGATE(SB_IML6ERR) { ForwardRequestInterrupts(); }

HOLLY_W32_DELEGATE(SB_C2DST) {
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

HOLLY_W32_DELEGATE(SB_SDST) {
  if (!reg.value) {
    return;
  }

  LOG_FATAL("Sort DMA not supported");
}

HOLLY_W32_DELEGATE(SB_GDST) {
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

HOLLY_W32_DELEGATE(SB_ADEN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored aica DMA request");
}

HOLLY_W32_DELEGATE(SB_ADST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored aica DMA request");
}

HOLLY_W32_DELEGATE(SB_E1EN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored ext1 DMA request");
}

HOLLY_W32_DELEGATE(SB_E1ST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored ext1 DMA request");
}

HOLLY_W32_DELEGATE(SB_E2EN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored ext2 DMA request");
}

HOLLY_W32_DELEGATE(SB_E2ST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored ext2 DMA request");
}

HOLLY_W32_DELEGATE(SB_DDEN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored dev DMA request");
}

HOLLY_W32_DELEGATE(SB_DDST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored dev DMA request");
}

HOLLY_W32_DELEGATE(SB_PDEN) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored pvr DMA request");
}

HOLLY_W32_DELEGATE(SB_PDST) {
  if (!reg.value) {
    return;
  }

  LOG_WARNING("Ignored pvr DMA request");
}
