#include "core/core.h"
#include "emu/profiler.h"
#include "hw/sh4/sh4.h"
#include "hw/memory.h"
#include "jit/runtime.h"

using namespace dreavm;
using namespace dreavm::emu;
using namespace dreavm::hw;
using namespace dreavm::hw::sh4;
using namespace dreavm::jit;
using namespace dreavm::jit::frontend::sh4;

InterruptInfo interrupts[NUM_INTERRUPTS] = {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) \
  { intevt, pri, ipr, ipr_shift }                  \
  ,
#include "hw/sh4/sh4_int.inc"
#undef SH4_INT
};

SH4::SH4(Memory &memory, Runtime &runtime)
    : memory_(memory),
      runtime_(runtime),
      pending_cache_reset_(false),
      requested_interrupts_(0),
      pending_interrupts_(0) {}

bool SH4::Init() {
  memset(&ctx_, 0, sizeof(ctx_));
  ctx_.priv = this;
  ctx_.SRUpdated = &SH4::SRUpdated;
  ctx_.FPSCRUpdated = &SH4::FPSCRUpdated;
  ctx_.pc = 0xa0000000;
  ctx_.pr = 0x0;
  ctx_.sr.full = ctx_.old_sr.full = 0x700000f0;
  ctx_.fpscr.full = ctx_.old_fpscr.full = 0x00040001;

  memset(area7_, 0, sizeof(area7_));
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  if (default != HELD) {                                                 \
    *(uint32_t *)&area7_[name##_OFFSET] = default;                       \
  }
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

  memset(cache_, 0, sizeof(cache_));

  ReprioritizeInterrupts();

  return true;
}

void SH4::SetPC(uint32_t pc) { ctx_.pc = pc; }

int SH4::Run(int cycles) {
  PROFILER_RUNTIME("SH4::Execute");

  int remaining = cycles;

  // TMU runs on the peripheral clock which is 50mhz vs our 200mhz
  for (int i = 0; i < 3; i++) {
    RunTimer(i, cycles >> 2);
  }

  while (ctx_.pc && remaining > 0) {
    RuntimeBlock *block = runtime_.GetBlock(ctx_.pc);

    ctx_.pc = block->call(&memory_, &ctx_, &runtime_, block, ctx_.pc);
    remaining -= block->guest_cycles;

    CheckPendingCacheReset();
    CheckPendingInterrupts();
  }

  return cycles - remaining;
}

void SH4::DDT(int channel, DDTRW rw, uint32_t addr) {
  CHECK_EQ(2, channel);

  uint32_t src_addr, dst_addr;
  if (rw == DDT_R) {
    src_addr = addr;
    dst_addr = DAR2;
  } else {
    src_addr = SAR2;
    dst_addr = addr;
  }

  size_t transfer_size = DMATCR2 * 32;
  for (size_t i = 0; i < transfer_size / 4; i++) {
    memory_.W32(dst_addr, memory_.R32(src_addr));
    dst_addr += 4;
    src_addr += 4;
  }

  SAR2 = src_addr;
  DAR2 = dst_addr;
  DMATCR2 = 0;
  CHCR2.TE = 1;
  RequestInterrupt(SH4_INTC_DMTE2);
}

void SH4::RequestInterrupt(Interrupt intr) {
  requested_interrupts_ |= sort_id_[intr];
  UpdatePendingInterrupts();
}

void SH4::UnrequestInterrupt(Interrupt intr) {
  requested_interrupts_ &= ~sort_id_[intr];
  UpdatePendingInterrupts();
}

template uint8_t SH4::ReadRegister(void *ctx, uint32_t addr);
template uint16_t SH4::ReadRegister(void *ctx, uint32_t addr);
template uint32_t SH4::ReadRegister(void *ctx, uint32_t addr);
template <typename T>
T SH4::ReadRegister(void *ctx, uint32_t addr) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx);

  // translate from 64mb space to our 16kb space
  addr = ((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2);

  switch (addr) {
    case PDTRA_OFFSET: {
      // magic values to get past 0x8c00b948 in the boot rom:
      // void _8c00b92c(int arg1) {
      //   sysvars->var1 = reg[PDTRA];
      //   for (i = 0; i < 4; i++) {
      //     sysvars->var2 = reg[PDTRA];
      //     if (arg1 == sysvars->var2 & 0x03) {
      //       return;
      //     }
      //   }
      //   reg[PR] = (uint32_t *)0x8c000000;    /* loop forever */
      // }
      // old_PCTRA = reg[PCTRA];
      // i = old_PCTRA | 0x08;
      // reg[PCTRA] = i;
      // reg[PDTRA] = reg[PDTRA] | 0x03;
      // _8c00b92c(3);
      // reg[PCTRA] = i | 0x03;
      // _8c00b92c(3);
      // reg[PDTRA] = reg[PDTRA] & 0xfffe;
      // _8c00b92c(0);
      // reg[PCTRA] = i;
      // _8c00b92c(3);
      // reg[PCTRA] = i | 0x04;
      // _8c00b92c(3);
      // reg[PDTRA] = reg[PDTRA] & 0xfffd;
      // _8c00b92c(0);
      // reg[PCTRA] = old_PCTRA;
      uint32_t pctra = self->PCTRA;
      uint32_t pdtra = self->PDTRA;
      uint32_t v = 0;
      if ((pctra & 0xf) == 0x8 ||
          ((pctra & 0xf) == 0xb && (pdtra & 0xf) != 0x2) ||
          ((pctra & 0xf) == 0xc && (pdtra & 0xf) == 0x2)) {
        v = 3;
      }
      // FIXME cable setting
      // When a VGA cable* is connected
      // 1. The SH4 obtains the cable information from the PIO port.  (PB[9:8] =
      // "00")
      // 2. Set the HOLLY synchronization register for VGA.  (The SYNC output is
      // H-Sync and V-Sync.)
      // 3. When VREG1 = 0 and VREG0 = 0 are written in the AICA register,
      // VIDEO1 = 0 and VIDEO0 = 1 are output.  VIDEO0 is connected to the
      // DVE-DACH pin, and handles switching between RGB and NTSC/PAL.
      //
      // When an RGB(NTSC/PAL) cable* is connected
      // 1. The SH4 obtains the cable information from the PIO port.  (PB[9:8] =
      // "10")
      // 2. Set the HOLLY synchronization register for NTSC/PAL.  (The SYNC
      // output is H-Sync and V-Sync.)
      // 3. When VREG1 = 0 and VREG0 = 0 are written in the AICA register,
      // VIDEO1 = 1 and VIDEO0 = 0 are output.  VIDEO0 is connected to the
      // DVE-DACH pin, and handles switching between RGB and NTSC/PAL.
      //
      // When a stereo A/V cable, an S-jack cable* or an RF converter* is
      // connected
      // 1. The SH4 obtains the cable information from the PIO port.  (PB[9:8] =
      // "11")
      // 2. Set the HOLLY synchronization register for NTSC/PAL.  (The SYNC
      // output is H-Sync and V-Sync.)
      // 3. When VREG1 = 1 and VREG0 = 1 are written in the AICA register,
      // VIDEO1 = 0 and VIDEO0 = 0 are output.  VIDEO0 is connected to the
      // DVE-DACH pin, and handles switching between RGB and NTSC/PAL.
      // v |= 0x3 << 8;
      return v;
    }
  }

  return static_cast<T>(self->area7_[addr]);
}

template void SH4::WriteRegister(void *ctx, uint32_t addr, uint8_t value);
template void SH4::WriteRegister(void *ctx, uint32_t addr, uint16_t value);
template void SH4::WriteRegister(void *ctx, uint32_t addr, uint32_t value);
template <typename T>
void SH4::WriteRegister(void *ctx, uint32_t addr, T value) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx);

  // translate from 64mb space to our 16kb space
  addr = ((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2);

  self->area7_[addr] = static_cast<uint32_t>(value);

  switch (addr) {
    case MMUCR_OFFSET:
      if (value) {
        LOG_FATAL("MMU not currently supported");
      }
      break;

    // it seems the only aspect of the cache control register that needs to be
    // emulated is the instruction cache invalidation
    case CCR_OFFSET:
      if (self->CCR.ICI) {
        self->ResetCache();
      }
      break;

    // when a PREF instruction is encountered, the high order bits of the
    // address are filled in from the queue address control register
    case QACR0_OFFSET:
      self->ctx_.sq_ext_addr[0] = (value & 0x1c) << 24;
      break;
    case QACR1_OFFSET:
      self->ctx_.sq_ext_addr[1] = (value & 0x1c) << 24;
      break;

    case IPRA_OFFSET:
    case IPRB_OFFSET:
    case IPRC_OFFSET:
      self->ReprioritizeInterrupts();
      break;

      // TODO UnrequestInterrupt on TCR write
  }
}

// with OIX, bit 25, rather than bit 13, determines which 4kb bank to use
#define CACHE_OFFSET(addr, OIX) \
  ((OIX ? ((addr & 0x2000000) >> 13) : ((addr & 0x2000) >> 1)) | (addr & 0xfff))

template uint8_t SH4::ReadCache(void *ctx, uint32_t addr);
template uint16_t SH4::ReadCache(void *ctx, uint32_t addr);
template uint32_t SH4::ReadCache(void *ctx, uint32_t addr);
template uint64_t SH4::ReadCache(void *ctx, uint32_t addr);
template <typename T>
T SH4::ReadCache(void *ctx, uint32_t addr) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx);
  CHECK_EQ(self->CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, self->CCR.OIX);
  return *reinterpret_cast<T *>(&self->cache_[addr]);
}

template void SH4::WriteCache(void *ctx, uint32_t addr, uint8_t value);
template void SH4::WriteCache(void *ctx, uint32_t addr, uint16_t value);
template void SH4::WriteCache(void *ctx, uint32_t addr, uint32_t value);
template void SH4::WriteCache(void *ctx, uint32_t addr, uint64_t value);
template <typename T>
void SH4::WriteCache(void *ctx, uint32_t addr, T value) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx);
  CHECK_EQ(self->CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, self->CCR.OIX);
  *reinterpret_cast<T *>(&self->cache_[addr]) = value;
}

template uint8_t SH4::ReadSQ(void *ctx, uint32_t addr);
template uint16_t SH4::ReadSQ(void *ctx, uint32_t addr);
template uint32_t SH4::ReadSQ(void *ctx, uint32_t addr);
template <typename T>
T SH4::ReadSQ(void *ctx, uint32_t addr) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx);
  uint32_t sqi = (addr & 0x20) >> 5;
  unsigned idx = (addr & 0x1c) >> 2;
  return static_cast<T>(self->ctx_.sq[sqi][idx]);
}

template void SH4::WriteSQ(void *ctx, uint32_t addr, uint8_t value);
template void SH4::WriteSQ(void *ctx, uint32_t addr, uint16_t value);
template void SH4::WriteSQ(void *ctx, uint32_t addr, uint32_t value);
template <typename T>
void SH4::WriteSQ(void *ctx, uint32_t addr, T value) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx);
  uint32_t sqi = (addr & 0x20) >> 5;
  unsigned idx = (addr & 0x1c) >> 2;
  self->ctx_.sq[sqi][idx] = static_cast<uint32_t>(value);
}

void SH4::SRUpdated(SH4Context *ctx) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx->priv);

  if (ctx->sr.RB != ctx->old_sr.RB) {
    self->SetRegisterBank(ctx->sr.RB ? 1 : 0);
  }

  if (ctx->sr.IMASK != ctx->old_sr.IMASK || ctx->sr.BL != ctx->old_sr.BL) {
    self->UpdatePendingInterrupts();
  }

  ctx->old_sr = ctx->sr;
}

void SH4::FPSCRUpdated(SH4Context *ctx) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx->priv);

  if (ctx->fpscr.FR != ctx->old_fpscr.FR) {
    self->SwapFPRegisters();
  }

  if (ctx->fpscr.PR != ctx->old_fpscr.PR) {
    self->SwapFPCouples();
  }

  ctx->old_fpscr = ctx->fpscr;
}

void SH4::SetRegisterBank(int bank) {
  if (bank == 0) {
    for (int s = 0; s < 8; s++) {
      uint32_t tmp = ctx_.r[s];
      ctx_.r[s] = ctx_.ralt[s];
      ctx_.ralt[s] = tmp;
    }
  } else if (bank == 1) {
    for (int s = 0; s < 8; s++) {
      uint32_t tmp = ctx_.r[s];
      ctx_.r[s] = ctx_.ralt[s];
      ctx_.ralt[s] = tmp;
    }
  }
}

void SH4::SwapFPRegisters() {
  uint32_t z;

  for (int s = 0; s <= 15; s++) {
    z = ctx_.fr[s];
    ctx_.fr[s] = ctx_.xf[s];
    ctx_.xf[s] = z;
  }
}

void SH4::SwapFPCouples() {
  uint32_t z;

  for (int s = 0; s <= 15; s = s + 2) {
    z = ctx_.fr[s];
    ctx_.fr[s] = ctx_.fr[s + 1];
    ctx_.fr[s + 1] = z;

    z = ctx_.xf[s];
    ctx_.xf[s] = ctx_.xf[s + 1];
    ctx_.xf[s + 1] = z;
  }
}

// FIXME this isn't right. When the IC is reset a pending flag is set and the
// cache is actually reset at the end of the current block. However, the docs
// for the SH4 IC state "After CCR is updated, an instruction that performs data
// access to the P0, P1, P3, or U0 area should be located at least four
// instructions after the CCR update instruction. Also, a branch instruction to
// the P0, P1, P3, or U0 area should be located at least eight instructions
// after the CCR update instruction."
void SH4::ResetCache() { pending_cache_reset_ = true; }

inline void SH4::CheckPendingCacheReset() {
  if (!pending_cache_reset_) {
    return;
  }

  runtime_.ResetBlocks();

  pending_cache_reset_ = false;
}

// Generate a sorted set of interrupts based on their priority. These sorted
// ids are used to represent all of the currently requested interrupts as a
// simple bitmask.
void SH4::ReprioritizeInterrupts() {
  uint64_t old = requested_interrupts_;
  requested_interrupts_ = 0;

  for (int i = 0, n = 0; i < 16; i++) {
    // for even priorities, give precedence to lower id interrupts
    for (int j = NUM_INTERRUPTS - 1; j >= 0; j--) {
      InterruptInfo &int_info = interrupts[j];

      // get current priority for interrupt
      int priority = int_info.default_priority;
      if (int_info.ipr) {
        uint16_t v = *reinterpret_cast<uint16_t *>(&area7_[int_info.ipr]);
        priority = (v >> int_info.ipr_shift) & 0xf;
      }

      if (priority != i) {
        continue;
      }

      bool was_requested = old & sort_id_[j];

      sorted_interrupts_[n] = (Interrupt)j;
      sort_id_[j] = (uint64_t)1 << n;
      n++;

      if (was_requested) {
        // rerequest with new sorted id
        requested_interrupts_ |= sort_id_[j];
      }
    }

    // generate a mask for all interrupts up to the current priority. used to
    // support SR.IMASK
    priority_mask_[i] = ((uint64_t)1 << n) - 1;
  }

  UpdatePendingInterrupts();
}

void SH4::UpdatePendingInterrupts() {
  int min_priority = ctx_.sr.IMASK;
  uint64_t priority_mask = ctx_.sr.BL ? 0 : ~priority_mask_[min_priority];
  pending_interrupts_ = requested_interrupts_ & priority_mask;
}

inline void SH4::CheckPendingInterrupts() {
  if (!pending_interrupts_) {
    return;
  }

  // process the highest priority in the pending vector
  int n = 63 - dreavm::clz(pending_interrupts_);
  Interrupt intr = sorted_interrupts_[n];
  InterruptInfo &int_info = interrupts[intr];

  INTEVT = int_info.intevt;
  ctx_.ssr = ctx_.sr;
  ctx_.spc = ctx_.pc;
  ctx_.sgr = ctx_.r[15];
  ctx_.sr.BL = 1;
  ctx_.sr.MD = 1;
  ctx_.sr.RB = 1;
  ctx_.pc = ctx_.vbr + 0x600;

  SRUpdated(&ctx_);
}

bool SH4::TimerEnabled(int n) {  //
  return TSTR & (1 << n);
}

void SH4::RunTimer(int n, int cycles) {
  static const int tcr_shift[] = {2, 4, 6, 8, 10, 0, 0, 0};

  if (!TimerEnabled(n)) {
    return;
  }

  uint32_t *tcor = nullptr;
  uint32_t *tcnt = nullptr;
  uint32_t *tcr = nullptr;
  Interrupt exception = (Interrupt)0;
  switch (n) {
    case 0:
      tcor = &TCOR0;
      tcnt = &TCNT0;
      tcr = &TCR0;
      exception = SH4_INTC_TUNI0;
      break;
    case 1:
      tcor = &TCOR1;
      tcnt = &TCNT1;
      tcr = &TCR1;
      exception = SH4_INTC_TUNI1;
      break;
    case 2:
      tcor = &TCOR2;
      tcnt = &TCNT2;
      tcr = &TCR2;
      exception = SH4_INTC_TUNI2;
      break;
    default:
      LOG_FATAL("Unexpected timer index %d", n);
      break;
  }

  // adjust cycles based on clock scale
  cycles >>= tcr_shift[*tcr & 7];

  // decrement timer
  bool underflowed = false;
  if ((*tcnt - cycles) > *tcnt) {
    cycles -= *tcnt;
    *tcnt = *tcor;
    underflowed = true;
  }
  *tcnt -= cycles;

  // set underflow flags and raise exception
  if (underflowed) {
    *tcnt = *tcor;
    *tcr |= 0x100;

    if (*tcr & 0x20) {
      RequestInterrupt(exception);
    }
  }
}
