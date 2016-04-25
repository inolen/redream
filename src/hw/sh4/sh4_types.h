#ifndef SH4_REGS_H
#define SH4_REGS_H

#include <stdint.h>

namespace re {
namespace hw {
namespace sh4 {

// registers
static const uint32_t UNDEFINED = 0x0;
static const uint32_t HELD = 0x1;

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

// control register area (0xfe000000 - 0xffffffff) seems to actually only
// represent 64 x 256 byte blocks of memory. the block index is represented
// by bits 17-24 and the block offset by bits 2-7
#define SH4_REG_OFFSET(addr) (((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2))

enum {
#define SH4_REG(addr, name, flags, default, reset, sleep, standby, type) \
  name##_OFFSET = SH4_REG_OFFSET(addr),
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG
  NUM_SH4_REGS = SH4_REG_OFFSET(0xffffffff) + 1
};

// interrupts
enum Interrupt {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) SH4_INTC_##name,
#include "hw/sh4/sh4_int.inc"
#undef SH4_INT
  NUM_INTERRUPTS
};

struct InterruptInfo {
  int intevt, default_priority, ipr, ipr_shift;
};
}
}
}

#endif
