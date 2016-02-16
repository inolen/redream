#include "core/math.h"
#include "core/memory.h"
#include "emu/profiler.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/scheduler.h"

using namespace re;
using namespace re::emu;
using namespace re::hw;
using namespace re::hw::sh4;
using namespace re::jit;
using namespace re::jit::frontend::sh4;

static InterruptInfo interrupts[NUM_INTERRUPTS] = {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) \
  { intevt, pri, ipr, ipr_shift }                  \
  ,
#include "hw/sh4/sh4_int.inc"
#undef SH4_INT
};

static SH4 *s_current_cpu = nullptr;

enum {
  SH4_CLOCK_FREQ = 200000000,
};

SH4::SH4(Dreamcast *dc)
    : dc_(dc),
      code_cache_(nullptr),
      pending_cache_reset_(false),
      requested_interrupts_(0),
      pending_interrupts_(0) {}

SH4::~SH4() { delete code_cache_; }

bool SH4::Init() {
  memory_ = dc_->memory;
  scheduler_ = dc_->scheduler;

  code_cache_ = new SH4CodeCache(memory_, &SH4::CompilePC);

  // initialize context
  memset(&ctx_, 0, sizeof(ctx_));
  ctx_.sh4 = this;
  ctx_.Pref = &SH4::Pref;
  ctx_.SRUpdated = &SH4::SRUpdated;
  ctx_.FPSCRUpdated = &SH4::FPSCRUpdated;
  ctx_.pc = 0xa0000000;
  ctx_.pr = 0x0;
  ctx_.sr = 0x700000f0;
  ctx_.fpscr = 0x00040001;

  // initialize registers
  memset(area7_, 0, sizeof(area7_));
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  if (default != HELD) {                                                 \
    *(uint32_t *)&area7_[name##_OFFSET] = default;                       \
  }
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

  // clear cache
  memset(cache_, 0, sizeof(cache_));

  // reset interrupts
  ReprioritizeInterrupts();

  // register timer
  TimerHandle timer =
      scheduler_->AllocTimer(std::bind(&SH4::Run, this, std::placeholders::_1));
  scheduler_->AdjustTimer(timer, std::chrono::nanoseconds(446428));

  // initialize tmu timers
  for (int i = 0; i < 3; i++) {
    tmu_timers_[i] = scheduler_->AllocTimer(
        [this, i](const std::chrono::nanoseconds &) { ExpireTimer(i); });
  }

  return true;
}

void SH4::SetPC(uint32_t pc) { ctx_.pc = pc; }

void SH4::Run(const std::chrono::nanoseconds &period) {
  PROFILER_RUNTIME("SH4::Execute");

  // execute at least 1 cycle. the tests rely on this to step block by block
  int64_t cycles = std::max(NANO_TO_CYCLES(period, SH4_CLOCK_FREQ), 1ll);

  // set current sh4 instance for CompilePC
  s_current_cpu = this;

  // each block's epilog will decrement the remaining cycles as they run
  ctx_.remaining_cycles = cycles;

  while (ctx_.remaining_cycles > 0) {
    BlockEntry *block = code_cache_->GetBlock(ctx_.pc);
    ctx_.pc = block->run();

    CheckPendingCacheReset();
    CheckPendingInterrupts();
  }

  s_current_cpu = nullptr;
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
    memory_->W32(dst_addr, memory_->R32(src_addr));
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

uint32_t SH4::CompilePC() {
  SH4CodeCache *code_cache = s_current_cpu->code_cache_;
  SH4Context *ctx = &s_current_cpu->ctx_;
  BlockEntry *block = code_cache->CompileBlock(ctx->pc, ctx);
  return block->run();
}

void SH4::Pref(SH4Context *ctx, uint64_t arg0) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx->sh4);
  uint32_t addr = static_cast<uint32_t>(arg0);

  // only concerned about SQ related prefetches
  if (addr < 0xe0000000 || addr > 0xe3ffffff) {
    return;
  }

  // figure out the source and destination
  uint32_t dest = addr & 0x03ffffe0;
  uint32_t sqi = (addr & 0x20) >> 5;
  if (sqi) {
    dest |= (self->QACR1 & 0x1c) << 24;
  } else {
    dest |= (self->QACR0 & 0x1c) << 24;
  }

  // perform the "burst" 32-byte copy
  for (int i = 0; i < 8; i++) {
    self->memory_->W32(dest, ctx->sq[sqi][i]);
    dest += 4;
  }
}

void SH4::SRUpdated(SH4Context *ctx, uint64_t old_sr) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx->sh4);

  if ((ctx->sr & RB) != (old_sr & RB)) {
    self->SwapRegisterBank();
  }

  if ((ctx->sr & I) != (old_sr & I) || (ctx->sr & BL) != (old_sr & BL)) {
    self->UpdatePendingInterrupts();
  }
}

void SH4::FPSCRUpdated(SH4Context *ctx, uint64_t old_fpscr) {
  SH4 *self = reinterpret_cast<SH4 *>(ctx->sh4);

  if ((ctx->fpscr & FR) != (old_fpscr & FR)) {
    self->SwapFPRegisterBank();
  }
}

void SH4::SwapRegisterBank() {
  for (int s = 0; s < 8; s++) {
    uint32_t tmp = ctx_.r[s];
    ctx_.r[s] = ctx_.ralt[s];
    ctx_.ralt[s] = tmp;
  }
}

void SH4::SwapFPRegisterBank() {
  for (int s = 0; s <= 15; s++) {
    uint32_t tmp = ctx_.fr[s];
    ctx_.fr[s] = ctx_.xf[s];
    ctx_.xf[s] = tmp;
  }
}

template uint8_t SH4::ReadRegister(uint32_t addr);
template uint16_t SH4::ReadRegister(uint32_t addr);
template uint32_t SH4::ReadRegister(uint32_t addr);
template <typename T>
T SH4::ReadRegister(uint32_t addr) {
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

    case TCNT0_OFFSET:
    case TCNT1_OFFSET:
    case TCNT2_OFFSET: {
      int n = addr == TCNT0_OFFSET ? 0 : addr == TCNT1_OFFSET ? 1 : 2;
      return TimerCount(n);
    }
  }

  return static_cast<T>(area7_[addr]);
}

template void SH4::WriteRegister(uint32_t addr, uint8_t value);
template void SH4::WriteRegister(uint32_t addr, uint16_t value);
template void SH4::WriteRegister(uint32_t addr, uint32_t value);
template <typename T>
void SH4::WriteRegister(uint32_t addr, T value) {
  // translate from 64mb space to our 16kb space
  addr = ((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2);

  area7_[addr] = static_cast<uint32_t>(value);

  switch (addr) {
    case MMUCR_OFFSET: {
      if (value) {
        LOG_FATAL("MMU not currently supported");
      }
    } break;

    // it seems the only aspect of the cache control register that needs to be
    // emulated is the instruction cache invalidation
    case CCR_OFFSET: {
      if (CCR.ICI) {
        ResetCache();
      }
    } break;

    case IPRA_OFFSET:
    case IPRB_OFFSET:
    case IPRC_OFFSET: {
      ReprioritizeInterrupts();
    } break;

    case TSTR_OFFSET: {
      UpdateTimerStart();
    } break;

    case TCR0_OFFSET:
    case TCR1_OFFSET:
    case TCR2_OFFSET: {
      int n = addr == TCR0_OFFSET ? 0 : addr == TCR1_OFFSET ? 1 : 2;
      UpdateTimerControl(n);
    } break;

    case TCNT0_OFFSET:
    case TCNT1_OFFSET:
    case TCNT2_OFFSET: {
      int n = addr == TCNT0_OFFSET ? 0 : addr == TCNT1_OFFSET ? 1 : 2;
      UpdateTimerCount(n);
    } break;
  }
}

// with OIX, bit 25, rather than bit 13, determines which 4kb bank to use
#define CACHE_OFFSET(addr, OIX) \
  ((OIX ? ((addr & 0x2000000) >> 13) : ((addr & 0x2000) >> 1)) | (addr & 0xfff))

template uint8_t SH4::ReadCache(uint32_t addr);
template uint16_t SH4::ReadCache(uint32_t addr);
template uint32_t SH4::ReadCache(uint32_t addr);
template uint64_t SH4::ReadCache(uint32_t addr);
template <typename T>
T SH4::ReadCache(uint32_t addr) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  return re::load<T>(&cache_[addr]);
}

template void SH4::WriteCache(uint32_t addr, uint8_t value);
template void SH4::WriteCache(uint32_t addr, uint16_t value);
template void SH4::WriteCache(uint32_t addr, uint32_t value);
template void SH4::WriteCache(uint32_t addr, uint64_t value);
template <typename T>
void SH4::WriteCache(uint32_t addr, T value) {
  CHECK_EQ(CCR.ORA, 1u);
  addr = CACHE_OFFSET(addr, CCR.OIX);
  re::store(&cache_[addr], value);
}

template uint8_t SH4::ReadSQ(uint32_t addr);
template uint16_t SH4::ReadSQ(uint32_t addr);
template uint32_t SH4::ReadSQ(uint32_t addr);
template <typename T>
T SH4::ReadSQ(uint32_t addr) {
  uint32_t sqi = (addr & 0x20) >> 5;
  unsigned idx = (addr & 0x1c) >> 2;
  return static_cast<T>(ctx_.sq[sqi][idx]);
}

template void SH4::WriteSQ(uint32_t addr, uint8_t value);
template void SH4::WriteSQ(uint32_t addr, uint16_t value);
template void SH4::WriteSQ(uint32_t addr, uint32_t value);
template <typename T>
void SH4::WriteSQ(uint32_t addr, T value) {
  uint32_t sqi = (addr & 0x20) >> 5;
  uint32_t idx = (addr & 0x1c) >> 2;
  ctx_.sq[sqi][idx] = static_cast<uint32_t>(value);
}

//
// CCN
//

void SH4::ResetCache() {
  // FIXME this isn't right. When the IC is reset a pending flag is set and the
  // cache is actually reset at the end of the current block. However, the docs
  // for the SH4 IC state "After CCR is updated, an instruction that performs
  // data
  // access to the P0, P1, P3, or U0 area should be located at least four
  // instructions after the CCR update instruction. Also, a branch instruction
  // to
  // the P0, P1, P3, or U0 area should be located at least eight instructions
  // after the CCR update instruction."
  pending_cache_reset_ = true;
}

inline void SH4::CheckPendingCacheReset() {
  if (!pending_cache_reset_) {
    return;
  }

  code_cache_->ResetBlocks();

  pending_cache_reset_ = false;
}

//
// INTC
//

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
        uint16_t v = re::load<uint16_t>(&area7_[int_info.ipr]);
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

    // generate a mask for all interrupts up to the current priority
    priority_mask_[i] = ((uint64_t)1 << n) - 1;
  }

  UpdatePendingInterrupts();
}

void SH4::UpdatePendingInterrupts() {
  int min_priority = (ctx_.sr & I) >> 4;
  uint64_t priority_mask = (ctx_.sr & BL) ? 0 : ~priority_mask_[min_priority];
  pending_interrupts_ = requested_interrupts_ & priority_mask;
}

inline void SH4::CheckPendingInterrupts() {
  if (!pending_interrupts_) {
    return;
  }

  // process the highest priority in the pending vector
  int n = 63 - re::clz(pending_interrupts_);
  Interrupt intr = sorted_interrupts_[n];
  InterruptInfo &int_info = interrupts[intr];

  INTEVT = int_info.intevt;
  ctx_.ssr = ctx_.sr;
  ctx_.spc = ctx_.pc;
  ctx_.sgr = ctx_.r[15];
  ctx_.sr |= (BL | MD | RB);
  ctx_.pc = ctx_.vbr + 0x600;

  SRUpdated(&ctx_, ctx_.ssr);
}

//
// TMU
//
static const int64_t PERIPHERAL_CLOCK_FREQ = SH4_CLOCK_FREQ >> 2;
static const int PERIPHERAL_SCALE[] = {2, 4, 6, 8, 10, 0, 0, 0};

#define TSTR(n) (TSTR & (1 << n))
#define TCOR(n) (n == 0 ? TCOR0 : n == 1 ? TCOR1 : TCOR2)
#define TCNT(n) (n == 0 ? TCNT0 : n == 1 ? TCNT1 : TCNT2)
#define TCR(n) (n == 0 ? TCR0 : n == 1 ? TCR1 : TCR2)
#define TUNI(n) \
  (n == 0 ? SH4_INTC_TUNI0 : n == 1 ? SH4_INTC_TUNI1 : SH4_INTC_TUNI2)

void SH4::UpdateTimerStart() {
  for (int i = 0; i < 3; i++) {
    TimerHandle &handle = tmu_timers_[i];

    if (TSTR(i)) {
      // schedule the timer if not already started
      if (scheduler_->RemainingTime(handle) == SUSPENDED) {
        ScheduleTimer(i, TCNT(i), TCR(i));
      }
    } else {
      // disable the timer
      scheduler_->AdjustTimer(handle, SUSPENDED);
    }
  }
}

void SH4::UpdateTimerControl(uint32_t n) {
  if (TSTR(n)) {
    // timer is already scheduled, reschedule it with the current cycle count,
    // but the new TCR value
    ScheduleTimer(n, TimerCount(n), TCR(n));
  }

  // if the timer no longer cares about underflow interrupts, unrequest
  if (!(TCR(n) & 0x20) || !(TCR(n) & 0x100)) {
    UnrequestInterrupt(TUNI(n));
  }
}

void SH4::UpdateTimerCount(uint32_t n) {
  if (TSTR(n)) {
    ScheduleTimer(n, TCNT(n), TCR(n));
  }
}

uint32_t SH4::TimerCount(int n) {
  // TCNT values aren't updated in real time. if a timer is enabled, query the
  // scheduler to figure out how many cycles are remaining for the given timer
  if (!TSTR(n)) {
    return TCNT(n);
  }

  // FIXME should the number of SH4 cycles that've been executed be considered
  // here? this would prevent an entire SH4 slice from just busy waiting on
  // this to change

  TimerHandle &handle = tmu_timers_[n];
  uint32_t tcr = TCR(n);

  int64_t freq = PERIPHERAL_CLOCK_FREQ >> PERIPHERAL_SCALE[tcr & 7];
  std::chrono::nanoseconds remaining = scheduler_->RemainingTime(handle);
  int64_t cycles = static_cast<uint32_t>(NANO_TO_CYCLES(remaining, freq));

  return cycles;
}

void SH4::ScheduleTimer(int n, uint32_t tcnt, uint32_t tcr) {
  TimerHandle &handle = tmu_timers_[n];

  int64_t freq = PERIPHERAL_CLOCK_FREQ >> PERIPHERAL_SCALE[tcr & 7];
  int64_t cycles = static_cast<int64_t>(tcnt);
  std::chrono::nanoseconds remaining = CYCLES_TO_NANO(cycles, freq);

  scheduler_->AdjustTimer(handle, remaining);
}

void SH4::ExpireTimer(int n) {
  uint32_t &tcor = TCOR(n);
  uint32_t &tcnt = TCNT(n);
  uint32_t &tcr = TCR(n);

  // timer expired, set the underflow flag
  tcr |= 0x100;

  // if interrupt generation on underflow is enabled, do so
  if (tcr & 0x20) {
    RequestInterrupt(TUNI(n));
  }

  // reset TCNT with the value from TCOR
  tcnt = tcor;

  // reschedule the timer with the new count
  ScheduleTimer(n, tcnt, tcr);
}
