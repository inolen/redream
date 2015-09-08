#include "core/core.h"
#include "cpu/runtime.h"
#include "cpu/sh4.h"
#include "emu/memory.h"
#include "emu/profiler.h"

using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::emu;

InterruptInfo interrupts[NUM_INTERRUPTS] = {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) \
  { intevt, pri, ipr, ipr_shift }                  \
  ,
#include "cpu/sh4_int.inc"
#undef SH4_INT
};

static void SetRegisterBank(SH4Context *ctx, int bank) {
  if (bank == 0) {
    for (int s = 0; s < 8; s++) {
      ctx->rbnk[1][s] = ctx->r[s];
      ctx->r[s] = ctx->rbnk[0][s];
    }
  } else {
    for (int s = 0; s < 8; s++) {
      ctx->rbnk[0][s] = ctx->r[s];
      ctx->r[s] = ctx->rbnk[1][s];
    }
  }
}

static void SwapFPRegisters(SH4Context *ctx) {
  uint32_t z;

  for (int s = 0; s <= 15; s++) {
    z = ctx->fr[s];
    ctx->fr[s] = ctx->xf[s];
    ctx->xf[s] = z;
  }
}

static void SwapFPCouples(SH4Context *ctx) {
  uint32_t z;

  for (int s = 0; s <= 15; s = s + 2) {
    z = ctx->fr[s];
    ctx->fr[s] = ctx->fr[s + 1];
    ctx->fr[s + 1] = z;

    z = ctx->xf[s];
    ctx->xf[s] = ctx->xf[s + 1];
    ctx->xf[s + 1] = z;
  }
}

void SH4Context::SRUpdated(SH4Context *ctx) {
  if (ctx->sr.RB != ctx->old_sr.RB) {
    SetRegisterBank(ctx, ctx->sr.RB ? 1 : 0);
  }

  ctx->old_sr = ctx->sr;
}

void SH4Context::FPSCRUpdated(SH4Context *ctx) {
  if (ctx->fpscr.FR != ctx->old_fpscr.FR) {
    SwapFPRegisters(ctx);
  }

  if (ctx->fpscr.PR != ctx->old_fpscr.PR) {
    SwapFPCouples(ctx);
  }

  ctx->old_fpscr = ctx->fpscr;
}

SH4::SH4(Memory &memory, Runtime &runtime)
    : memory_(memory),
      runtime_(runtime),
      pending_cache_reset_(false),
      requested_interrupts_(0),
      pending_interrupts_(0) {}

void SH4::Init() {
  memset(&ctx_, 0, sizeof(ctx_));
  ctx_.pc = 0xa0000000;
  ctx_.pr = 0x0;
  ctx_.sr.full = ctx_.old_sr.full = 0x700000f0;
  ctx_.fpscr.full = ctx_.old_fpscr.full = 0x00040001;

  memset(area7_, 0, sizeof(area7_));
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  if (default != HELD) {                                                 \
    *(uint32_t *)&area7_[name##_OFFSET] = default;                       \
  }
#include "cpu/sh4_regs.inc"
#undef SH4_REG

  memset(cache_, 0, sizeof(cache_));

  ReprioritizeInterrupts();
}

void SH4::SetPC(uint32_t pc) { ctx_.pc = pc; }

uint32_t SH4::Execute(uint32_t cycles) {
  PROFILER_RUNTIME("SH4::Execute");

  // LOG_INFO("Executing %d cycles at 0x%x", cycles, ctx_.pc);

  uint32_t remaining = cycles;

  // update timers
  for (int i = 0; i < 3; i++) {
    // TMU runs on the peripheral clock which is 50mhz vs our 200mhz
    RunTimer(i, cycles >> 2);
  }

  while (ctx_.pc) {
    uint32_t pc = ctx_.pc & ADDR_MASK;
    RuntimeBlock *block = runtime_.GetBlock(pc, &ctx_);

    // be careful not to wrap around
    uint32_t next_remaining = remaining - block->guest_cycles();
    if (next_remaining > remaining) {
      break;
    }

    // run the block
    uint32_t next_pc = block->Call(&memory_, &ctx_);
    remaining = next_remaining;
    ctx_.pc = next_pc;

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

uint8_t SH4::ReadRegister8(uint32_t addr) {
  return static_cast<uint8_t>(ReadRegister32(addr));
}

uint16_t SH4::ReadRegister16(uint32_t addr) {
  return static_cast<uint16_t>(ReadRegister32(addr));
}

uint32_t SH4::ReadRegister32(uint32_t addr) {
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
      uint32_t pctra = PCTRA;
      uint32_t pdtra = PDTRA;
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

  return area7_[addr];
}

void SH4::WriteRegister8(uint32_t addr, uint8_t value) {
  WriteRegister32(addr, static_cast<uint32_t>(value));
}

void SH4::WriteRegister16(uint32_t addr, uint16_t value) {
  WriteRegister32(addr, static_cast<uint32_t>(value));
}

void SH4::WriteRegister32(uint32_t addr, uint32_t value) {
  // translate from 64mb space to our 16kb space
  addr = ((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2);

  area7_[addr] = value;

  switch (addr) {
    case MMUCR_OFFSET:
      if (value) {
        LOG_FATAL("MMU not currently supported");
      }
      break;

    // it seems the only aspect of the cache control register that needs to be
    // emulated is the instruction cache invalidation
    case CCR_OFFSET:
      if (CCR.ICI) {
        ResetCache();
      }
      break;

    // when a PREF instruction is encountered, the high order bits of the
    // address are filled in from the queue address control register
    case QACR0_OFFSET:
      ctx_.sq_ext_addr[0] = (value & 0x1c) << 24;
      break;
    case QACR1_OFFSET:
      ctx_.sq_ext_addr[1] = (value & 0x1c) << 24;
      break;

    case IPRA_OFFSET:
    case IPRB_OFFSET:
    case IPRC_OFFSET:
      ReprioritizeInterrupts();
      break;

      // TODO UnrequestInterrupt on TCR write
  }
}

// with OIX, bit 25, rather than bit 13, determines which 4kb bank to use
#define CACHE_OFFSET(addr, OIX) \
  ((OIX ? ((addr & 0x2000000) >> 13) : ((addr & 0x2000) >> 1)) | (addr & 0xfff))

uint8_t SH4::ReadCache8(uint32_t addr) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  return *reinterpret_cast<uint8_t *>(&cache_[addr]);
}

uint16_t SH4::ReadCache16(uint32_t addr) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  return *reinterpret_cast<uint16_t *>(&cache_[addr]);
}

uint32_t SH4::ReadCache32(uint32_t addr) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  return *reinterpret_cast<uint32_t *>(&cache_[addr]);
}

uint64_t SH4::ReadCache64(uint32_t addr) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  return *reinterpret_cast<uint64_t *>(&cache_[addr]);
}

void SH4::WriteCache8(uint32_t addr, uint8_t value) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  *reinterpret_cast<uint8_t *>(&cache_[addr]) = value;
}

void SH4::WriteCache16(uint32_t addr, uint16_t value) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  *reinterpret_cast<uint16_t *>(&cache_[addr]) = value;
}

void SH4::WriteCache32(uint32_t addr, uint32_t value) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  *reinterpret_cast<uint32_t *>(&cache_[addr]) = value;
}

void SH4::WriteCache64(uint32_t addr, uint64_t value) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  *reinterpret_cast<uint64_t *>(&cache_[addr]) = value;
}

uint8_t SH4::ReadSQ8(uint32_t addr) {
  return static_cast<uint8_t>(ReadSQ32(addr));
}

uint16_t SH4::ReadSQ16(uint32_t addr) {
  return static_cast<uint16_t>(ReadSQ32(addr));
}

uint32_t SH4::ReadSQ32(uint32_t addr) {
  uint32_t sqi = (addr & 0x20) >> 5;
  unsigned idx = (addr & 0x1c) >> 2;
  return ctx_.sq[sqi][idx];
}

void SH4::WriteSQ8(uint32_t addr, uint8_t value) {
  WriteSQ32(addr, static_cast<uint32_t>(value));
}

void SH4::WriteSQ16(uint32_t addr, uint16_t value) {
  WriteSQ32(addr, static_cast<uint32_t>(value));
}

void SH4::WriteSQ32(uint32_t addr, uint32_t value) {
  uint32_t sqi = (addr & 0x20) >> 5;
  unsigned idx = (addr & 0x1c) >> 2;
  ctx_.sq[sqi][idx] = value;
}

// FIXME this isn't right. When the IC is reset a pending flag is set and the
// cache is actually reset at the end of the current block. However, the docs
// for the SH4 IC state "After CCR is updated, an instruction that performs data
// access to the P0, P1, P3, or U0 area should be located at least four
// instructions after the CCR update instruction. Also, a branch instruction to
// the P0, P1, P3, or U0 area should be located at least eight instructions
// after the CCR update instruction."
void SH4::ResetCache() { pending_cache_reset_ = true; }

void SH4::CheckPendingCacheReset() {
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

void SH4::CheckPendingInterrupts() {
  // update pending interrupts if the status register has changed
  if (ctx_.sr.full != old_sr_.full) {
    UpdatePendingInterrupts();
    old_sr_.full = ctx_.sr.full;
  }

  if (!pending_interrupts_) {
    return;
  }

  // process the highest priority in the pending vector
  int n = 63 - core::clz(pending_interrupts_);
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

  SH4Context::SRUpdated(&ctx_);
}

bool SH4::TimerEnabled(int n) {  //
  return TSTR & (1 << n);
}

void SH4::RunTimer(int n, uint32_t cycles) {
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
