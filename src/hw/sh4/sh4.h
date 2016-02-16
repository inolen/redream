#ifndef SH4_H
#define SH4_H

#include "hw/scheduler.h"
#include "hw/sh4/sh4_code_cache.h"
#include "jit/frontend/sh4/sh4_context.h"

struct SH4Test;

namespace re {
namespace hw {

struct Dreamcast;

extern bool MapMemory(Dreamcast &dc);

namespace sh4 {

// registers
enum {
  UNDEFINED = 0x0,
  HELD = 0x1,
};

union CCR_T {
  uint32_t full;
  struct {
    uint32_t OCE : 1;
    uint32_t WT : 1;
    uint32_t CB : 1;
    uint32_t OCI : 1;
    uint32_t reserved : 1;
    uint32_t ORA : 1;
    uint32_t reserved1 : 1;
    uint32_t OIX : 1;
    uint32_t ICE : 1;
    uint32_t reserved2 : 2;
    uint32_t ICI : 1;
    uint32_t reserved3 : 3;
    uint32_t IIX : 1;
    uint32_t reserved4 : 15;
    uint32_t EMODE : 1;
  };
};

union CHCR_T {
  uint32_t full;
  struct {
    uint32_t DE : 1;
    uint32_t TE : 1;
    uint32_t IE : 1;
    uint32_t QCL : 1;
    uint32_t TS : 3;
    uint32_t TM : 1;
    uint32_t RS : 4;
    uint32_t SM : 2;
    uint32_t DM : 2;
    uint32_t AL : 1;
    uint32_t AM : 1;
    uint32_t RL : 1;
    uint32_t DS : 1;
    uint32_t reserved : 4;
    uint32_t DTC : 1;
    uint32_t DSA : 3;
    uint32_t STC : 1;
    uint32_t SSA : 3;
  };
};

union DMAOR_T {
  uint32_t full;
  struct {
    uint32_t DME : 1;
    uint32_t NMIF : 1;
    uint32_t AE : 1;
    uint32_t reserved : 5;
    uint32_t PR0 : 1;
    uint32_t PR1 : 1;
    uint32_t reserved1 : 4;
    uint32_t DBL : 1;
    uint32_t DDT : 1;
    uint32_t reserved2 : 16;
  };
};

enum {
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  name##_OFFSET = ((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2),
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG
};

// interrupts
struct InterruptInfo {
  int intevt, default_priority, ipr, ipr_shift;
};

enum Interrupt {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) SH4_INTC_##name,
#include "hw/sh4/sh4_int.inc"
#undef SH4_INT
  NUM_INTERRUPTS
};

// DMAC
enum DDTRW {  //
  DDT_R,
  DDT_W
};

class SH4 {
  friend bool re::hw::MapMemory(Dreamcast &dc);
  friend void RunSH4Test(const SH4Test &);

 public:
  SH4(Dreamcast *dc);
  ~SH4();

  bool Init();
  void SetPC(uint32_t pc);
  void Run(const std::chrono::nanoseconds &period);

  // DMAC
  void DDT(int channel, DDTRW rw, uint32_t addr);

  // INTC
  void RequestInterrupt(Interrupt intr);
  void UnrequestInterrupt(Interrupt intr);

 private:
  static uint32_t CompilePC();
  static void Pref(jit::frontend::sh4::SH4Context *ctx, uint64_t addr);
  static void SRUpdated(jit::frontend::sh4::SH4Context *ctx, uint64_t old_sr);
  static void FPSCRUpdated(jit::frontend::sh4::SH4Context *ctx,
                           uint64_t old_fpscr);

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
  void CheckPendingCacheReset();

  // INTC
  void ReprioritizeInterrupts();
  void UpdatePendingInterrupts();
  void CheckPendingInterrupts();

  // TMU
  void UpdateTimerStart();
  void UpdateTimerControl(uint32_t n);
  void UpdateTimerCount(uint32_t n);
  uint32_t TimerCount(int n);
  void ScheduleTimer(int n, uint32_t tcnt, uint32_t tcr);
  void ExpireTimer(int n);

  Dreamcast *dc_;
  Memory *memory_;
  Scheduler *scheduler_;
  SH4CodeCache *code_cache_;

  jit::frontend::sh4::SH4Context ctx_;
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  type &name{reinterpret_cast<type &>(area7_[name##_OFFSET])};
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

  bool pending_cache_reset_;

  Interrupt sorted_interrupts_[NUM_INTERRUPTS];
  uint64_t sort_id_[NUM_INTERRUPTS];
  uint64_t priority_mask_[16];
  uint64_t requested_interrupts_;
  uint64_t pending_interrupts_;

  hw::TimerHandle tmu_timers_[3];

  uint32_t area7_[0x4000];  // consolidated, 16kb area 7 memory
  uint8_t cache_[0x2000];   // 8kb cache
};
}
}
}

#endif
