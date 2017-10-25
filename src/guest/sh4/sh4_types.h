#ifndef SH4_TYPES_H
#define SH4_TYPES_H

#include <stdint.h>

/*
 * registers
 */

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

union scsmr2 {
  uint32_t full;
  struct {
    uint32_t CKS : 2;
    uint32_t : 1;
    uint32_t STOP : 1;
    uint32_t OE : 1;
    uint32_t PE : 1;
    uint32_t CHR : 1;
    uint32_t : 25;
  };
};

union scscr2 {
  uint32_t full;
  struct {
    uint32_t : 1;
    uint32_t CKE1 : 1;
    uint32_t : 1;
    uint32_t REIE : 1;
    uint32_t RE : 1;
    uint32_t TE : 1;
    uint32_t RIE : 1;
    uint32_t TIE : 1;
    uint32_t : 24;
  };
};

union scfsr2 {
  uint32_t full;
  struct {
    uint32_t DR : 1;
    uint32_t RDF : 1;
    uint32_t PER : 1;
    uint32_t FER : 1;
    uint32_t BRK : 1;
    uint32_t TDFE : 1;
    uint32_t TEND : 1;
    uint32_t ER : 1;
    uint32_t FER0 : 1;
    uint32_t FER1 : 1;
    uint32_t FER2 : 1;
    uint32_t FER3 : 1;
    uint32_t PER0 : 1;
    uint32_t PER1 : 1;
    uint32_t PER2 : 1;
    uint32_t PER3 : 1;
    uint32_t : 16;
  };
};

union scfcr2 {
  uint32_t full;
  struct {
    uint32_t LOOP : 1;
    uint32_t RFRST : 1;
    uint32_t TFRST : 1;
    uint32_t MCE : 1;
    uint32_t TTRG : 2;
    uint32_t RTRG : 2;
    uint32_t RSTRG : 3;
    uint32_t : 21;
  };
};

union scfdr2 {
  uint32_t full;
  struct {
    uint32_t R : 5;
    uint32_t : 3;
    uint32_t T : 5;
    uint32_t : 19;
  };
};

union sclsr2 {
  uint32_t full;
  struct {
    uint32_t ORER : 1;
    uint32_t : 31;
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
  SH4_NUM_REGS = SH4_REG_OFFSET(0xffffffff) + 1
};

/*
 * interrupts
 */

enum sh4_exception {
#define SH4_EXC(name, expevt, offset, prilvl, priord) SH4_EXC_##name,
#include "guest/sh4/sh4_exc.inc"
#undef SH4_EXC
  SH4_NUM_EXCEPTIONS
};

enum sh4_interrupt {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) SH4_INT_##name,
#include "guest/sh4/sh4_int.inc"
#undef SH4_INT
  SH4_NUM_INTERRUPTS
};

struct sh4_exception_info {
  int expevt;
  int offset;
  int prilvl;
  int priord;
};

struct sh4_interrupt_info {
  int intevt;
  int default_priority;
  int ipr;
  int ipr_shift;
};

#endif
