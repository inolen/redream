#include "core/core.h"
#include "cpu/runtime.h"
#include "cpu/sh4.h"

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

void SH4Context::SetRegisterBank(int bank) {
  if (bank == 0) {
    for (int s = 0; s < 8; s++) {
      rbnk[1][s] = r[s];
      r[s] = rbnk[0][s];
    }
  } else {
    for (int s = 0; s < 8; s++) {
      rbnk[0][s] = r[s];
      r[s] = rbnk[1][s];
    }
  }
}

void SH4Context::SwapFPRegisters() {
  unsigned s;
  uint32_t z;

  for (s = 0; s <= 15; s++) {
    z = fr[s];
    fr[s] = xf[s];
    xf[s] = z;
  }
}

void SH4Context::SwapFPCouples() {
  int s;
  uint32_t z;

  for (s = 0; s <= 15; s = s + 2) {
    z = fr[s];
    fr[s] = fr[s + 1];
    fr[s + 1] = z;
    z = xf[s];
    xf[s] = xf[s + 1];
    xf[s + 1] = z;
  }
}

void SH4Context::SRUpdated() {
  if (sr.RB != old_sr.RB) {
    SetRegisterBank(sr.RB ? 1 : 0);
  }
  old_sr = sr;
  sh4->UpdatePendingInterrupts();
}

void SH4Context::FPSCRUpdated() {
  if (fpscr.FR != old_fpscr.FR) {
    SwapFPRegisters();
  }
  if (fpscr.PR != old_fpscr.PR) {
    SwapFPCouples();
  }
  old_fpscr = fpscr;
}

SH4::SH4(emu::Scheduler &scheduler, Memory &memory)
    : scheduler_(scheduler), memory_(memory) {}

SH4::~SH4() {}

bool SH4::Init(Runtime *runtime) {
  runtime_ = runtime;
  if (runtime_) {
    runtime_->ctx().guest_ctx = &ctx_;
  }

  scheduler_.AddDevice(this);

  InitMemory();
  InitContext();
  ReprioritizeInterrupts();

  return true;
}

int64_t SH4::Execute(int64_t cycles) {
  // LOG(INFO) << "Executing " << cycles << " cycles @ 0x" << std::hex <<
  // ctx_.pc;

  int64_t remaining = cycles;

  // update timers
  for (int i = 0; i < 3; i++) {
    // TMU runs on the peripheral clock which is 50mhz
    RunTimer(i, cycles >> 2);
  }

  // run cpu
  while (ctx_.pc != 0xdeadbeef && remaining > 0) {
    RuntimeBlock *block = runtime_->ResolveBlock(ctx_.pc);
    CHECK(block);

    uint32_t nextpc = block->Call(runtime_->ctx());
    remaining -= block->guest_cycles();
    ctx_.pc = nextpc;

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

// with OIX, bit 25, rather than bit 13, determines which 4kb bank to use
#define CACHE_OFFSET(addr, OIX) \
  ((OIX ? ((addr & 0x2000000) >> 13) : ((addr & 0x2000) >> 1)) | (addr & 0xfff))

template <typename T>
T SH4::ReadCache(void *ctx, uint32_t addr) {
  SH4 *sh4 = (SH4 *)ctx;
  CHECK_EQ(sh4->CCR.ORA, 1);
  uint32_t offset = CACHE_OFFSET(addr, sh4->CCR.OIX);
  return *reinterpret_cast<T *>(&sh4->cache_[offset]);
}

template <typename T>
void SH4::WriteCache(void *ctx, uint32_t addr, T value) {
  SH4 *sh4 = (SH4 *)ctx;
  CHECK_EQ(sh4->CCR.ORA, 1);
  uint32_t offset = CACHE_OFFSET(addr, sh4->CCR.OIX);
  *reinterpret_cast<T *>(&sh4->cache_[offset]) = value;
}

uint32_t SH4::ReadSQ(void *ctx, uint32_t addr) {
  SH4 *sh4 = (SH4 *)ctx;
  uint32_t sqi = (addr & 0x20) >> 5;
  unsigned idx = (addr & 0x1c) >> 2;
  return sh4->ctx_.sq[sqi][idx];
}

void SH4::WriteSQ(void *ctx, uint32_t addr, uint32_t value) {
  SH4 *sh4 = (SH4 *)ctx;
  uint32_t sqi = (addr & 0x20) >> 5;
  unsigned idx = (addr & 0x1c) >> 2;
  sh4->ctx_.sq[sqi][idx] = value;
}

uint32_t SH4::ReadArea7(void *ctx, uint32_t addr) {
  SH4 *sh4 = (SH4 *)ctx;

  // translate from 64mb space to our 16kb space
  addr = ((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2);

  switch (addr) {
    case MMUCR_OFFSET:
      // LOG(FATAL) << "MMU not supported currently.";
      break;

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
      uint32_t pctra = sh4->PCTRA;
      uint32_t pdtra = sh4->PDTRA;
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

  return sh4->ctx_.m[addr];
}

void SH4::WriteArea7(void *ctx, uint32_t addr, uint32_t value) {
  SH4 *sh4 = (SH4 *)ctx;

  // translate from 64mb space to our 16kb space
  addr = ((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2);

  sh4->ctx_.m[addr] = value;

  switch (addr) {
    case CCR_OFFSET:
      if (sh4->CCR.ICI) {
        sh4->ResetInstructionCache();
      }
      break;

    case IPRA_OFFSET:
    case IPRB_OFFSET:
    case IPRC_OFFSET:
      sh4->ReprioritizeInterrupts();
      break;

      // TODO UnrequestInterrupt on TCR write
  }
}

void SH4::InitMemory() {
  // map cache
  memory_.Handle(0x7c000000, 0x7fffffff, 0x0, this, &SH4::ReadCache<uint8_t>,
                 &SH4::ReadCache<uint16_t>, &SH4::ReadCache<uint32_t>,
                 &SH4::ReadCache<uint64_t>, &SH4::WriteCache<uint8_t>,
                 &SH4::WriteCache<uint16_t>, &SH4::WriteCache<uint32_t>,
                 &SH4::WriteCache<uint64_t>);

  // map store queues
  memory_.Handle(0xe0000000, 0xe3ffffff, 0x0, this, &SH4::ReadSQ,
                 &SH4::WriteSQ);

  // mount internal cpu register area
  memory_.Handle(0xfc000000, 0xffffffff, 0x0, this, &SH4::ReadArea7,
                 &SH4::WriteArea7);
}

void SH4::InitContext() {
  memset(&ctx_, 0, sizeof(ctx_));
  ctx_.sh4 = this;
  // ctx_.pc = 0xa0000000;
  ctx_.pc = 0x0c010000;
  ctx_.pr = 0xdeadbeef;
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  if (default != HELD) {                                                 \
    *(uint32_t *)&ctx_.m[name##_OFFSET] = default;                       \
  }
#include "cpu/sh4_regs.inc"
#undef SH4_REG
  ctx_.sr.full = ctx_.old_sr.full = 0x700000f0;
  ctx_.fpscr.full = ctx_.old_fpscr.full = 0x00040001;

  memset(cache_, 0, sizeof(cache_));
}

void SH4::ResetInstructionCache() { runtime_->ResetBlocks(); }

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
        uint16_t v = *reinterpret_cast<uint16_t *>(&ctx_.m[int_info.ipr]);
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
  if (!pending_interrupts_) {
    return;
  }

  // process the highest priority in the pending vector
  int n = 63 - __builtin_clzll(pending_interrupts_);
  Interrupt intr = sorted_interrupts_[n];
  InterruptInfo &int_info = interrupts[intr];

  INTEVT = int_info.intevt;
  ctx_.ssr = ctx_.sr;
  ctx_.spc = ctx_.pc;
  ctx_.sgr = ctx_.r[15];
  ctx_.sr.BL = 1;
  ctx_.sr.MD = 1;
  ctx_.sr.RB = 1;
  if (ctx_.old_sr.RB != ctx_.sr.RB) {
    ctx_.SetRegisterBank(1);
  }
  ctx_.old_sr = ctx_.sr;
  ctx_.pc = ctx_.vbr + 0x600;
  if (ctx_.sleep_mode == 1) {
    ctx_.sleep_mode = 2;
  }

  UpdatePendingInterrupts();
}

bool SH4::TimerEnabled(int n) {  //
  return TSTR & (1 << n);
}

void SH4::RunTimer(int n, int64_t cycles) {
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
  }

  // adjust cycles based on clock scale
  cycles >>= tcr_shift[*tcr & 7];

  // decrement timer
  bool underflowed = false;
  if ((uint32_t)(*tcnt - cycles) > *tcnt) {
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
