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

Holly::Holly(Dreamcast *dc) : Device(*dc), MemoryInterface(this), dc_(dc) {}

bool Holly::Init() {
  holly_regs_ = dc_->holly_regs;
  gdrom_ = dc_->gdrom;
  maple_ = dc_->maple;
  sh4_ = dc_->sh4;

// initialize registers
#define HOLLY_REG(addr, name, flags, default, type) \
  holly_regs_[name##_OFFSET] = {flags, default};
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG

  return true;
}

void Holly::RequestInterrupt(HollyInterrupt intr) {
  HollyInterruptType type =
      static_cast<HollyInterruptType>(intr & HOLLY_INTC_MASK);
  uint32_t irq = static_cast<uint32_t>(intr & ~HOLLY_INTC_MASK);

  if (intr == HOLLY_INTC_PCVOINT) {
    dc_->maple->VBlank();
  }

  switch (type) {
    case HOLLY_INTC_NRM:
      dc_->SB_ISTNRM |= irq;
      break;

    case HOLLY_INTC_EXT:
      dc_->SB_ISTEXT |= irq;
      break;

    case HOLLY_INTC_ERR:
      dc_->SB_ISTERR |= irq;
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
      dc_->SB_ISTNRM &= ~irq;
      break;

    case HOLLY_INTC_EXT:
      dc_->SB_ISTEXT &= ~irq;
      break;

    case HOLLY_INTC_ERR:
      dc_->SB_ISTERR &= ~irq;
      break;
  }

  ForwardRequestInterrupts();
}

void Holly::MapPhysicalMemory(Memory &memory, MemoryMap &memmap) {
  RegionHandle holly_handle = memory.AllocRegion(
      HOLLY_REG_START, HOLLY_REG_SIZE,
      make_delegate(&Holly::ReadRegister<uint8_t>, this),
      make_delegate(&Holly::ReadRegister<uint16_t>, this),
      make_delegate(&Holly::ReadRegister<uint32_t>, this), nullptr,
      make_delegate(&Holly::WriteRegister<uint8_t>, this),
      make_delegate(&Holly::WriteRegister<uint16_t>, this),
      make_delegate(&Holly::WriteRegister<uint32_t>, this), nullptr);

  memmap.Mount(holly_handle, HOLLY_REG_SIZE, HOLLY_REG_START);
}

template <typename T>
T Holly::ReadRegister(uint32_t addr) {
  uint32_t offset = addr >> 2;
  Register &reg = holly_regs_[offset];

  // service all devices connected through the system bus
  if (offset >= SB_MDSTAR_OFFSET && offset <= SB_MRXDBD_OFFSET) {
    return maple_->ReadRegister<T>(addr);
  }
  if (offset >= GD_ALTSTAT_DEVCTRL_OFFSET && offset <= SB_GDLEND_OFFSET) {
    return gdrom_->ReadRegister<T>(addr);
  }

  if (!(reg.flags & R)) {
    LOG_WARNING("Invalid read access at 0x%x", addr);
    return 0;
  }

  switch (offset) {
    case SB_ISTNRM_OFFSET: {
      // Note that the two highest bits indicate the OR'ed result of all of the
      // bits in SB_ISTEXT and SB_ISTERR, respectively, and writes to these two
      // bits are ignored.
      uint32_t v = reg.value & 0x3fffffff;
      if (dc_->SB_ISTEXT) {
        v |= 0x40000000;
      }
      if (dc_->SB_ISTERR) {
        v |= 0x80000000;
      }
      return static_cast<T>(v);
    } break;
  }

  return static_cast<T>(reg.value);
}

template <typename T>
void Holly::WriteRegister(uint32_t addr, T value) {
  uint32_t offset = addr >> 2;
  Register &reg = holly_regs_[offset];

  // service all devices connected through the system bus
  if (offset >= SB_MDSTAR_OFFSET && offset <= SB_MRXDBD_OFFSET) {
    maple_->WriteRegister<T>(addr, value);
    return;
  }
  if (offset >= GD_ALTSTAT_DEVCTRL_OFFSET && offset <= SB_GDLEND_OFFSET) {
    gdrom_->WriteRegister<T>(addr, value);
    return;
  }

  if (!(reg.flags & W)) {
    LOG_WARNING("Invalid write access at 0x%x", addr);
    return;
  }

  uint32_t old = reg.value;
  reg.value = static_cast<uint32_t>(value);

  switch (offset) {
    case SB_ISTNRM_OFFSET:
    case SB_ISTEXT_OFFSET:
    case SB_ISTERR_OFFSET: {
      // writing a 1 clears the interrupt
      reg.value = old & ~value;
      ForwardRequestInterrupts();
    } break;

    case SB_IML2NRM_OFFSET:
    case SB_IML2EXT_OFFSET:
    case SB_IML2ERR_OFFSET:
    case SB_IML4NRM_OFFSET:
    case SB_IML4EXT_OFFSET:
    case SB_IML4ERR_OFFSET:
    case SB_IML6NRM_OFFSET:
    case SB_IML6EXT_OFFSET:
    case SB_IML6ERR_OFFSET:
      ForwardRequestInterrupts();
      break;

    case SB_C2DST_OFFSET:
      if (value) {
        CH2DMATransfer();
      }
      break;

    case SB_SDST_OFFSET:
      if (value) {
        SortDMATransfer();
      }
      break;

    case SB_ADEN_OFFSET:
    case SB_ADST_OFFSET:
    case SB_E1EN_OFFSET:
    case SB_E1ST_OFFSET:
    case SB_E2EN_OFFSET:
    case SB_E2ST_OFFSET:
    case SB_DDEN_OFFSET:
    case SB_DDST_OFFSET:
      if (value) {
        LOG_WARNING("Ignored AICA DMA request");
      }
      break;

    case SB_PDEN_OFFSET:
    case SB_PDST_OFFSET:
      if (value) {
        // NOTE PVR DMA can invalidate texture cache
        LOG_WARNING("Ignored PVR DMA request");
      }
      break;

    default:
      break;
  }
}

// FIXME what are SB_LMMODE0 / SB_LMMODE1
void Holly::CH2DMATransfer() {
  sh4_->DDT(2, DDT_W, dc_->SB_C2DSTAT);

  dc_->SB_C2DLEN = 0;
  dc_->SB_C2DST = 0;
  RequestInterrupt(HOLLY_INTC_DTDE2INT);
}

void Holly::SortDMATransfer() {
  dc_->SB_SDST = 0;
  RequestInterrupt(HOLLY_INTC_DTDESINT);
}

void Holly::ForwardRequestInterrupts() {
  // trigger the respective level-encoded interrupt on the sh4 interrupt
  // controller
  {
    if ((dc_->SB_ISTNRM & dc_->SB_IML6NRM) ||
        (dc_->SB_ISTERR & dc_->SB_IML6ERR) ||
        (dc_->SB_ISTEXT & dc_->SB_IML6EXT)) {
      sh4_->RequestInterrupt(SH4_INTC_IRL_9);
    } else {
      sh4_->UnrequestInterrupt(SH4_INTC_IRL_9);
    }
  }

  {
    if ((dc_->SB_ISTNRM & dc_->SB_IML4NRM) ||
        (dc_->SB_ISTERR & dc_->SB_IML4ERR) ||
        (dc_->SB_ISTEXT & dc_->SB_IML4EXT)) {
      sh4_->RequestInterrupt(SH4_INTC_IRL_11);
    } else {
      sh4_->UnrequestInterrupt(SH4_INTC_IRL_11);
    }
  }

  {
    if ((dc_->SB_ISTNRM & dc_->SB_IML2NRM) ||
        (dc_->SB_ISTERR & dc_->SB_IML2ERR) ||
        (dc_->SB_ISTEXT & dc_->SB_IML2EXT)) {
      sh4_->RequestInterrupt(SH4_INTC_IRL_13);
    } else {
      sh4_->UnrequestInterrupt(SH4_INTC_IRL_13);
    }
  }
}
