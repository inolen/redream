#include "core/core.h"
#include "cpu/sh4.h"
#include "holly/gdrom.h"
#include "holly/holly.h"
#include "holly/maple.h"
#include "holly/pvr2.h"

using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::emu;
using namespace dreavm::holly;
using namespace dreavm::system;

Holly::Holly(Scheduler &scheduler, Memory &memory, SH4 &sh4)
    : memory_(memory),
      sh4_(sh4),
      pvr_(scheduler, memory, *this),
      gdrom_(memory, *this),
      maple_(memory, sh4, *this) {
  modem_mem_ = new uint8_t[MODEM_REG_SIZE];
  aica_mem_ = new uint8_t[AICA_REG_SIZE];
  audio_mem_ = new uint8_t[AUDIO_RAM_SIZE];
  expdev_mem_ = new uint8_t[EXPDEV_SIZE];
}

Holly::~Holly() {
  delete[] modem_mem_;
  delete[] aica_mem_;
  delete[] audio_mem_;
  delete[] expdev_mem_;
}

bool Holly::Init(renderer::Backend *rb) {
  InitMemory();

  if (!pvr_.Init(rb)) {
    return false;
  }

  if (!gdrom_.Init()) {
    return false;
  }

  if (!maple_.Init()) {
    return false;
  }

  Reset();

  return true;
}

void Holly::RequestInterrupt(Interrupt intr) {
  InterruptType type = (InterruptType)(intr & HOLLY_INTC_MASK);
  uint32_t irq = intr & ~HOLLY_INTC_MASK;

  if (intr == HOLLY_INTC_PCVOINT) {
    maple_.VBlank();
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

void Holly::UnrequestInterrupt(Interrupt intr) {
  InterruptType type = (InterruptType)(intr & HOLLY_INTC_MASK);
  uint32_t irq = intr & ~HOLLY_INTC_MASK;

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

namespace dreavm {
namespace holly {

template <typename T>
T Holly::ReadRegister(void *ctx, uint32_t addr) {
  return static_cast<T>(ReadRegister<uint32_t>(ctx, addr));
}

template <>
uint32_t Holly::ReadRegister(void *ctx, uint32_t addr) {
  Holly *holly = (Holly *)ctx;
  Register &reg = holly->regs_[addr >> 2];

  if (!(reg.flags & R)) {
    LOG(FATAL) << "Invalid read access at 0x" << std::hex << addr;
  }

  if (addr >= SB_MDSTAR_OFFSET && addr <= SB_MRXDBD_OFFSET) {
    return holly->maple_.ReadRegister(reg, addr);
  } else if (addr >= GD_ALTSTAT_DEVCTRL_OFFSET && addr <= SB_GDLEND_OFFSET) {
    return holly->gdrom_.ReadRegister(reg, addr);
  }

  switch (reg.offset) {
    case SB_ISTNRM_OFFSET: {
      // Note that the two highest bits indicate the OR'ed result of all of the
      // bits in SB_ISTEXT and SB_ISTERR, respectively, and writes to these two
      // bits are ignored.
      uint32_t v = reg.value & 0x3fffffff;
      if (holly->SB_ISTEXT) {
        v |= 0x40000000;
      }
      if (holly->SB_ISTERR) {
        v |= 0x80000000;
      }
      return v;
    } break;
  }

  return reg.value;
}

template <typename T>
void Holly::WriteRegister(void *ctx, uint32_t addr, T value) {
  WriteRegister<uint32_t>(ctx, addr, static_cast<uint32_t>(value));
}

template <>
void Holly::WriteRegister(void *ctx, uint32_t addr, uint32_t value) {
  Holly *holly = (Holly *)ctx;
  Register &reg = holly->regs_[addr >> 2];

  if (!(reg.flags & W)) {
    LOG(FATAL) << "Invalid write access at 0x" << std::hex << addr;
  }

  if (addr >= SB_MDSTAR_OFFSET && addr <= SB_MRXDBD_OFFSET) {
    holly->maple_.WriteRegister(reg, addr, value);
    return;
  } else if (addr >= GD_ALTSTAT_DEVCTRL_OFFSET && addr <= SB_GDLEND_OFFSET) {
    holly->gdrom_.WriteRegister(reg, addr, value);
    return;
  }

  uint32_t old = reg.value;
  reg.value = value;

  switch (reg.offset) {
    case SB_ISTNRM_OFFSET:
    case SB_ISTEXT_OFFSET:
    case SB_ISTERR_OFFSET: {
      // writing a 1 clears the interrupt
      reg.value = old & ~value;
      holly->ForwardRequestInterrupts();
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
      holly->ForwardRequestInterrupts();
      break;

    case SB_C2DST_OFFSET:
      if (value) {
        holly->CH2DMATransfer();
      }
      break;

    case SB_SDST_OFFSET:
      if (value) {
        holly->SortDMATransfer();
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
    case SB_PDEN_OFFSET:
    case SB_PDST_OFFSET:
      if (value) {
        LOG(INFO) << "AICA DMA request ignored";
      }
      break;

    default:
      break;
  }
}

}
}

void Holly::InitMemory() {
  memory_.Handle(HOLLY_REG_START, HOLLY_REG_END, MIRROR_MASK, this,
                 &Holly::ReadRegister<uint8_t>, &Holly::ReadRegister<uint16_t>,
                 &Holly::ReadRegister<uint32_t>, nullptr,
                 &Holly::WriteRegister<uint8_t>,
                 &Holly::WriteRegister<uint16_t>,
                 &Holly::WriteRegister<uint32_t>, nullptr);
  memory_.Mount(MODEM_REG_START, MODEM_REG_END, MIRROR_MASK, modem_mem_);
  memory_.Mount(AICA_REG_START, AICA_REG_END, MIRROR_MASK, aica_mem_);
  memory_.Mount(AUDIO_RAM_START, AUDIO_RAM_END, MIRROR_MASK, audio_mem_);
  memory_.Mount(EXPDEV_START, EXPDEV_END, MIRROR_MASK, expdev_mem_);
}

void Holly::Reset() {
  memset(modem_mem_, 0, MODEM_REG_SIZE);
  memset(aica_mem_, 0, AICA_REG_SIZE);
  memset(audio_mem_, 0, AUDIO_RAM_SIZE);
  memset(expdev_mem_, 0, EXPDEV_SIZE);

// initialize registers
#define HOLLY_REG(addr, name, flags, default, type) \
  regs_[name##_OFFSET >> 2] = {name##_OFFSET, flags, default};
#include "holly/holly_regs.inc"
#undef HOLLY_REG
}

// FIXME what are SB_LMMODE0 / SB_LMMODE1
void Holly::CH2DMATransfer() {
  sh4_.DDT(2, DDT_W, SB_C2DSTAT);

  SB_C2DLEN = 0;
  SB_C2DST = 0;
  RequestInterrupt(HOLLY_INTC_DTDE2INT);
}

void Holly::SortDMATransfer() {
  SB_SDST = 0;
  RequestInterrupt(HOLLY_INTC_DTDESINT);
}

void Holly::ForwardRequestInterrupts() {
  // trigger the respective level-encoded interrupt on the sh4 interrupt
  // controller
  {
    if ((SB_ISTNRM & SB_IML6NRM) || (SB_ISTERR & SB_IML6ERR) ||
        (SB_ISTEXT & SB_IML6EXT)) {
      sh4_.RequestInterrupt(SH4_INTC_IRL_9);
    } else {
      sh4_.UnrequestInterrupt(SH4_INTC_IRL_9);
    }
  }

  {
    if ((SB_ISTNRM & SB_IML4NRM) || (SB_ISTERR & SB_IML4ERR) ||
        (SB_ISTEXT & SB_IML4EXT)) {
      sh4_.RequestInterrupt(SH4_INTC_IRL_11);
    } else {
      sh4_.UnrequestInterrupt(SH4_INTC_IRL_11);
    }
  }

  {
    if ((SB_ISTNRM & SB_IML2NRM) || (SB_ISTERR & SB_IML2ERR) ||
        (SB_ISTEXT & SB_IML2EXT)) {
      sh4_.RequestInterrupt(SH4_INTC_IRL_13);
    } else {
      sh4_.UnrequestInterrupt(SH4_INTC_IRL_13);
    }
  }
}
