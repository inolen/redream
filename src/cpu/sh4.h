#ifndef SH4_H
#define SH4_H

#include "cpu/sh4_context.h"
#include "emu/device.h"
#include "emu/memory.h"
#include "emu/scheduler.h"

struct SH4Test;

namespace dreavm {
namespace cpu {

class Runtime;

// registers
enum {
  UNDEFINED = 0x0,
  HELD = 0x1,
};

enum {
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  name##_OFFSET = ((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2),
#include "cpu/sh4_regs.inc"
#undef SH4_REG
};

union CCR_T {
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
  uint32_t full;
};

union CHCR_T {
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
  uint32_t full;
};

union DMAOR_T {
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
  uint32_t full;
};

// interrupts
struct InterruptInfo {
  int intevt, default_priority, ipr, ipr_shift;
};

enum Interrupt {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) SH4_INTC_##name,
#include "cpu/sh4_int.inc"
#undef SH4_INT
  NUM_INTERRUPTS
};

// DMAC
enum DDTRW {  //
  DDT_R,
  DDT_W
};

class SH4 : public emu::Device {
  friend void SRUpdated(SH4Context *ctx);
  friend void RunSH4Test(const SH4Test &);

 public:
  SH4(emu::Scheduler &scheduler, emu::Memory &memory);
  virtual ~SH4();

  int64_t GetClockFrequency() { return 200000000; }

  bool Init(Runtime *runtime);
  void Reset(uint32_t pc);
  int64_t Execute(int64_t cycles);

  // DMAC
  void DDT(int channel, DDTRW rw, uint32_t addr);

  // INTC
  void RequestInterrupt(Interrupt intr);
  void UnrequestInterrupt(Interrupt intr);

 protected:
  template <typename T>
  static T ReadCache(void *ctx, uint32_t addr);
  template <typename T>
  static void WriteCache(void *ctx, uint32_t addr, T value);
  template <typename T>
  static T ReadSQ(void *ctx, uint32_t addr);
  template <typename T>
  static void WriteSQ(void *ctx, uint32_t addr, T value);
  template <typename T>
  static T ReadArea7(void *ctx, uint32_t addr);
  template <typename T>
  static void WriteArea7(void *ctx, uint32_t addr, T value);

  void InitMemory();
  void ResetState();

  // CCN
  void ResetInstructionCache();

  // INTC
  void ReprioritizeInterrupts();
  void UpdatePendingInterrupts();
  void CheckPendingInterrupts();

  Interrupt sorted_interrupts_[NUM_INTERRUPTS];
  uint64_t sort_id_[NUM_INTERRUPTS];
  uint64_t priority_mask_[16];
  uint64_t requested_interrupts_;
  uint64_t pending_interrupts_;

  // TMU
  bool TimerEnabled(int n);
  void RunTimer(int n, int64_t cycles);

  emu::Scheduler &scheduler_;
  emu::Memory &memory_;
  Runtime *runtime_;
  SH4Context ctx_;
  SR_T old_sr_;
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  type &name{reinterpret_cast<type &>(area7_[name##_OFFSET])};
#include "cpu/sh4_regs.inc"
#undef SH4_REG

  uint32_t area7_[0x4000];  // consolidated, 16kb area 7 memory
  uint8_t cache_[0x2000];   // 8kb cache
};
}
}

#endif
