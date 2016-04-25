#ifndef SH4_H
#define SH4_H

#include <chrono>
#include "hw/sh4/sh4_code_cache.h"
#include "hw/sh4/sh4_types.h"
#include "hw/machine.h"
#include "hw/memory.h"
#include "hw/register.h"
#include "hw/scheduler.h"
#include "jit/frontend/sh4/sh4_context.h"

struct SH4Test;

namespace re {
namespace hw {

class Dreamcast;

namespace sh4 {

static const int MAX_MIPS_SAMPLES = 10;

// data transfer request
struct DTR {
  DTR() : channel(0), rw(false), data(nullptr), addr(0), size(0) {}

  int channel;
  // when rw is true, addr is the dst address
  // when rw is false, addr is the src address
  bool rw;
  // when data is non-null, a single address mode transfer is performed between
  // the external device memory at data, and the memory at addr for
  // when data is null, a dual address mode transfer is performed between addr
  // and SARn / DARn
  uint8_t *data;
  uint32_t addr;
  // size is only valid for single address mode transfers, dual address mode
  // transfers honor DMATCR
  int size;
};

class SH4 : public Device,
            public DebugInterface,
            public ExecuteInterface,
            public MemoryInterface,
            public WindowInterface {
  friend void RunSH4Test(const SH4Test &);

 public:
  AM_DECLARE(data_map);

  SH4(Dreamcast &dc);
  ~SH4();

  bool Init() final;
  void SetPC(uint32_t pc);

  // ExecuteInterface
  void Run(const std::chrono::nanoseconds &delta) final;

  // DMAC
  void DDT(const DTR &dtr);

  // INTC
  void RequestInterrupt(Interrupt intr);
  void UnrequestInterrupt(Interrupt intr);

#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  type &name = reinterpret_cast<type &>(regs_[name##_OFFSET].value);
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

 private:
  // DebugInterface
  int NumRegisters() final;
  void Step() final;
  void AddBreakpoint(int type, uint32_t addr) final;
  void RemoveBreakpoint(int type, uint32_t addr) final;
  void ReadMemory(uint32_t addr, uint8_t *buffer, int size) final;
  void ReadRegister(int n, uint64_t *value, int *size) final;

  // WindowInterface
  void OnPaint(bool show_main_menu) final;

  static uint32_t CompilePC();
  static void InvalidInstruction(jit::frontend::sh4::SH4Context *ctx,
                                 uint64_t addr);
  static void Prefetch(jit::frontend::sh4::SH4Context *ctx, uint64_t addr);
  static void SRUpdated(jit::frontend::sh4::SH4Context *ctx, uint64_t old_sr);
  static void FPSCRUpdated(jit::frontend::sh4::SH4Context *ctx,
                           uint64_t old_fpscr);

  int GetCompileFlags();
  void SwapRegisterBank();
  void SwapFPRegisterBank();

  template <typename T>
  T ReadRegister(uint32_t addr);
  template <typename T>
  void WriteRegister(uint32_t addr, T value);

  template <typename T>
  T ReadCache(uint32_t addr);
  template <typename T>
  void WriteCache(uint32_t addr, T value);

  template <typename T>
  T ReadSQ(uint32_t addr);
  template <typename T>
  void WriteSQ(uint32_t addr, T value);

  // CCN
  void ResetCache();

  // DMAC
  void CheckDMA(int channel);

  // INTC
  void ReprioritizeInterrupts();
  void UpdatePendingInterrupts();
  void CheckPendingInterrupts();

  // TMU
  void UpdateTimerStart();
  void UpdateTimerControl(uint32_t n);
  void UpdateTimerCount(uint32_t n);
  uint32_t TimerCount(int n);
  void RescheduleTimer(int n, uint32_t tcnt, uint32_t tcr);
  template <int N>
  void ExpireTimer();

  DECLARE_R32_DELEGATE(PDTRA);
  DECLARE_W32_DELEGATE(MMUCR);
  DECLARE_W32_DELEGATE(CCR);
  DECLARE_W32_DELEGATE(CHCR0);
  DECLARE_W32_DELEGATE(CHCR1);
  DECLARE_W32_DELEGATE(CHCR2);
  DECLARE_W32_DELEGATE(CHCR3);
  DECLARE_W32_DELEGATE(DMAOR);
  DECLARE_W32_DELEGATE(IPRA);
  DECLARE_W32_DELEGATE(IPRB);
  DECLARE_W32_DELEGATE(IPRC);
  DECLARE_W32_DELEGATE(TSTR);
  DECLARE_W32_DELEGATE(TCR0);
  DECLARE_W32_DELEGATE(TCR1);
  DECLARE_W32_DELEGATE(TCR2);
  DECLARE_R32_DELEGATE(TCNT0);
  DECLARE_W32_DELEGATE(TCNT0);
  DECLARE_R32_DELEGATE(TCNT1);
  DECLARE_W32_DELEGATE(TCNT1);
  DECLARE_R32_DELEGATE(TCNT2);
  DECLARE_W32_DELEGATE(TCNT2);

  Dreamcast &dc_;
  Scheduler *scheduler_;
  SH4CodeCache *code_cache_;

  jit::frontend::sh4::SH4Context ctx_;
  Register regs_[NUM_SH4_REGS];
  uint8_t cache_[0x2000];  // 8kb cache
  std::map<uint32_t, uint16_t> breakpoints_;

  bool show_perf_;
  std::chrono::high_resolution_clock::time_point last_mips_time_;
  float mips_[MAX_MIPS_SAMPLES];
  int num_mips_;

  Interrupt sorted_interrupts_[NUM_INTERRUPTS];
  uint64_t sort_id_[NUM_INTERRUPTS];
  uint64_t priority_mask_[16];
  uint64_t requested_interrupts_;
  uint64_t pending_interrupts_;

  hw::TimerHandle tmu_timers_[3];
  TimerDelegate tmu_delegates_[3];
};
}
}
}

#endif
