#ifndef SH4_TYPES_H
#define SH4_TYPES_H

#include <stdint.h>

union pteh {
  uint32_t full;
  struct {
    uint32_t ASID : 8;
    uint32_t : 2;
    uint32_t VPN : 22;
  };
};

union ptel {
  uint32_t full;
  struct {
    uint32_t WT : 1;
    uint32_t SH : 1;
    uint32_t D : 1;
    uint32_t C : 1;
    uint32_t SZ0 : 1;
    uint32_t PR : 2;
    uint32_t SZ1 : 1;
    uint32_t V : 1;
    uint32_t : 1;
    uint32_t PPN : 19;
    uint32_t : 3;
  };
};

union mmucr {
  uint32_t full;
  struct {
    uint32_t AT : 1;
    uint32_t : 1;
    uint32_t TI : 1;
    uint32_t : 5;
    uint32_t SV : 1;
    uint32_t SQMD : 1;
    uint32_t URC : 6;
    uint32_t URB : 6;
    uint32_t LRUI : 6;
  };
};

union ccr {
  uint32_t full;
  struct {
    uint32_t OCE : 1;
    uint32_t WT : 1;
    uint32_t CB : 1;
    uint32_t OCI : 1;
    uint32_t : 1;
    uint32_t ORA : 1;
    uint32_t : 1;
    uint32_t OIX : 1;
    uint32_t ICE : 1;
    uint32_t : 2;
    uint32_t ICI : 1;
    uint32_t : 3;
    uint32_t IIX : 1;
    uint32_t : 15;
    uint32_t EMODE : 1;
  };
};

union chcr {
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
    uint32_t : 4;
    uint32_t DTC : 1;
    uint32_t DSA : 3;
    uint32_t STC : 1;
    uint32_t SSA : 3;
  };
};

union dmaor {
  uint32_t full;
  struct {
    uint32_t DME : 1;
    uint32_t NMIF : 1;
    uint32_t AE : 1;
    uint32_t : 5;
    uint32_t PR0 : 1;
    uint32_t PR1 : 1;
    uint32_t : 4;
    uint32_t DBL : 1;
    uint32_t DDT : 1;
    uint32_t : 16;
  };
};

union stbcr {
  uint32_t full;
  struct {
    uint32_t MSTP0 : 1;
    uint32_t MSTP1 : 1;
    uint32_t MSTP2 : 1;
    uint32_t MSTP3 : 1;
    uint32_t MSTP4 : 1;
    uint32_t PPU : 1;
    uint32_t PHZ : 1;
    uint32_t STBY : 1;
    uint32_t : 24;
  };
};

union stbcr2 {
  uint32_t full;
  struct {
    uint32_t : 7;
    uint32_t DSLP : 1;
    uint32_t : 24;
  };
};

/* control register area (0xfc000000 - 0xffffffff) contains only 16kb of
   physical memory. this memory is mapped as 64 x 256 byte blocks, with the
   block index being encoded in bits 17-24 of the address, and the block
   offset offset in bits 2-7 */
#define SH4_REG_OFFSET(addr) (((addr & 0x1fe0000) >> 11) | ((addr & 0xfc) >> 2))

enum {
#define SH4_REG(addr, name, default, type) name = SH4_REG_OFFSET(addr),
#include "guest/sh4/sh4_regs.inc"
#undef SH4_REG
  NUM_SH4_REGS = SH4_REG_OFFSET(0xffffffff) + 1
};

enum sh4_interrupt {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) SH4_INT_##name,
#include "guest/sh4/sh4_int.inc"
#undef SH4_INT
  NUM_SH_INTERRUPTS
};

#endif
