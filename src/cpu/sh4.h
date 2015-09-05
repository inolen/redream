#ifndef SH4_H
#define SH4_H

#include "emu/device.h"

struct SH4Test;

namespace dreavm {
namespace emu {
class Memory;
}

namespace cpu {

class Runtime;

// translate address to 29-bit physical space, ignoring all modifier bits
enum { ADDR_MASK = 0x1fffffff };

// registers
enum {
  UNDEFINED = 0x0,
  HELD = 0x1,
};

union SR_T {
  uint32_t full;
  struct {
    uint32_t T : 1;
    uint32_t S : 1;
    uint32_t reserved : 2;
    uint32_t IMASK : 4;
    uint32_t Q : 1;
    uint32_t M : 1;
    uint32_t reserved1 : 5;
    uint32_t FD : 1;
    uint32_t reserved2 : 12;
    uint32_t BL : 1;
    uint32_t RB : 1;
    uint32_t MD : 1;
    uint32_t reserved3 : 1;
  };
};

union FPSCR_T {
  uint32_t full;
  struct {
    uint32_t RM : 2;
    uint32_t flag : 5;
    uint32_t enable : 5;
    uint32_t cause : 6;
    uint32_t DN : 1;
    uint32_t PR : 1;
    uint32_t SZ : 1;
    uint32_t FR : 1;
    uint32_t reserved : 10;
  };
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
#include "cpu/sh4_regs.inc"
#undef SH4_REG
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

struct SH4Context {
  static void SRUpdated(SH4Context *ctx);
  static void FPSCRUpdated(SH4Context *ctx);

  uint32_t pc, spc;
  uint32_t pr;
  uint32_t gbr, vbr;
  uint32_t mach, macl;
  uint32_t r[16], rbnk[2][8], sgr;
  uint32_t fr[16], xf[16];
  uint32_t fpul;
  uint32_t dbr;
  uint32_t sq[2][8];
  uint32_t sq_ext_addr[2];
  uint32_t preserve;
  SR_T sr, ssr, old_sr;
  FPSCR_T fpscr, old_fpscr;
};

class SH4 : public emu::Device {
  template <typename BACKEND>
  friend void RunSH4Test(const SH4Test &);

 public:
  SH4(emu::Memory &memory, cpu::Runtime &runtime);

  uint32_t GetClockFrequency() { return 200000000; }

  void Init();
  void SetPC(uint32_t pc);
  uint32_t Execute(uint32_t cycles);

  // DMAC
  void DDT(int channel, DDTRW rw, uint32_t addr);

  // INTC
  void RequestInterrupt(Interrupt intr);
  void UnrequestInterrupt(Interrupt intr);

  uint8_t ReadRegister8(uint32_t addr);
  uint16_t ReadRegister16(uint32_t addr);
  uint32_t ReadRegister32(uint32_t addr);
  void WriteRegister8(uint32_t addr, uint8_t value);
  void WriteRegister16(uint32_t addr, uint16_t value);
  void WriteRegister32(uint32_t addr, uint32_t value);

  uint8_t ReadCache8(uint32_t addr);
  uint16_t ReadCache16(uint32_t addr);
  uint32_t ReadCache32(uint32_t addr);
  uint64_t ReadCache64(uint32_t addr);
  void WriteCache8(uint32_t addr, uint8_t value);
  void WriteCache16(uint32_t addr, uint16_t value);
  void WriteCache32(uint32_t addr, uint32_t value);
  void WriteCache64(uint32_t addr, uint64_t value);

  uint8_t ReadSQ8(uint32_t addr);
  uint16_t ReadSQ16(uint32_t addr);
  uint32_t ReadSQ32(uint32_t addr);

  void WriteSQ8(uint32_t addr, uint8_t value);
  void WriteSQ16(uint32_t addr, uint16_t value);
  void WriteSQ32(uint32_t addr, uint32_t value);

 protected:
  // CCN
  void ResetInstructionCache();

  // INTC
  void ReprioritizeInterrupts();
  void UpdatePendingInterrupts();
  void CheckPendingInterrupts();

  // TMU
  bool TimerEnabled(int n);
  void RunTimer(int n, uint32_t cycles);

  emu::Memory &memory_;
  Runtime &runtime_;

  SH4Context ctx_;
  SR_T old_sr_;
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  type &name{reinterpret_cast<type &>(area7_[name##_OFFSET])};
#include "cpu/sh4_regs.inc"
#undef SH4_REG

  Interrupt sorted_interrupts_[NUM_INTERRUPTS];
  uint64_t sort_id_[NUM_INTERRUPTS];
  uint64_t priority_mask_[16];
  uint64_t requested_interrupts_;
  uint64_t pending_interrupts_;

  uint32_t area7_[0x4000];  // consolidated, 16kb area 7 memory
  uint8_t cache_[0x2000];   // 8kb cache
};
}
}

#endif
