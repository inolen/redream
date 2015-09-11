#ifndef HOLLY_H
#define HOLLY_H

#include <stdint.h>

namespace dreavm {
namespace hw {
namespace sh4 {
class SH4;
}

class Dreamcast;
struct Register;

namespace holly {

// interrupts
#define HOLLY_INTC_MASK 0xf00000000

enum HollyInterruptType : uint64_t {
  HOLLY_INTC_NRM = 0x100000000,
  HOLLY_INTC_EXT = 0x200000000,
  HOLLY_INTC_ERR = 0x300000000
};

enum HollyInterrupt : uint64_t {
  //
  // HOLLY_INTC_NRM
  //
  // Video End of Render
  HOLLY_INTC_PCEOVINT = HOLLY_INTC_NRM | 0x1,
  // ISP End of Render
  HOLLY_INTC_PCEOIINT = HOLLY_INTC_NRM | 0x2,
  // TSP End of Render
  HOLLY_INTC_PCEOTINT = HOLLY_INTC_NRM | 0x4,
  // VBlank In
  HOLLY_INTC_PCVIINT = HOLLY_INTC_NRM | 0x8,
  // VBlank Out
  HOLLY_INTC_PCVOINT = HOLLY_INTC_NRM | 0x10,
  // HBlank In
  HOLLY_INTC_PCHIINT = HOLLY_INTC_NRM | 0x20,
  // End Of YUV Data Storage
  HOLLY_INTC_TAYUVINT = HOLLY_INTC_NRM | 0x40,
  // End Of Opaque List Storage
  HOLLY_INTC_TAEOINT = HOLLY_INTC_NRM | 0x80,
  // End Of Opaque Modifier Volume List Storage
  HOLLY_INTC_TAEOMINT = HOLLY_INTC_NRM | 0x100,
  // End Of Translucent List Storage
  HOLLY_INTC_TAETINT = HOLLY_INTC_NRM | 0x200,
  // End Of Translucent Modifier Volume List Storage
  HOLLY_INTC_TAETMINT = HOLLY_INTC_NRM | 0x400,
  // PVR End of DMA
  HOLLY_INTC_PIDEINT = HOLLY_INTC_NRM | 0x800,
  // MAPLE End of DMA
  HOLLY_INTC_MDEINT = HOLLY_INTC_NRM | 0x1000,
  // MAPLE VBlank Over
  HOLLY_INTC_MVOINT = HOLLY_INTC_NRM | 0x2000,
  // G1 End of DMA
  HOLLY_INTC_G1DEINT = HOLLY_INTC_NRM | 0x4000,
  // G2 End of AICA-DMA
  HOLLY_INTC_G2DEAINT = HOLLY_INTC_NRM | 0x8000,
  // G2 End of Ext-DMA1
  HOLLY_INTC_G2DE1INT = HOLLY_INTC_NRM | 0x10000,
  // G2 End of Ext-DMA2
  HOLLY_INTC_G2DE2INT = HOLLY_INTC_NRM | 0x20000,
  // G2 End of Dev-DMA
  HOLLY_INTC_G2DEDINT = HOLLY_INTC_NRM | 0x40000,
  // End of ch2-DMA
  HOLLY_INTC_DTDE2INT = HOLLY_INTC_NRM | 0x80000,
  // End of Sort-DMA
  HOLLY_INTC_DTDESINT = HOLLY_INTC_NRM | 0x100000,
  // End Of Punch Through List Storage
  HOLLY_INTC_TAEPTIN = HOLLY_INTC_NRM | 0x200000,
  //
  // HOLLY_INTC_EXT
  //
  // From GD-ROM Drive
  HOLLY_INTC_G1GDINT = HOLLY_INTC_EXT | 0x1,
  // From AICA
  HOLLY_INTC_G2AICINT = HOLLY_INTC_EXT | 0x2,
  // From Modem
  HOLLY_INTC_G2MDMINT = HOLLY_INTC_EXT | 0x4,
  // From External Device
  HOLLY_INTC_G2EXTINT = HOLLY_INTC_EXT | 0x8,
  //
  // HOLLY_INTC_ERR
  //
  // ISP Out of Cache
  HOLLY_INTC_PCIOCINT = HOLLY_INTC_ERR | 0x1,
  // Hazard Processing of Strip Buffer
  HOLLY_INTC_PCHZDINT = HOLLY_INTC_ERR | 0x2,
  // ISP/TSP Parameter Limit Address
  HOLLY_INTC_TAPOFINT = HOLLY_INTC_ERR | 0x4,
  // Object List Limit Address
  HOLLY_INTC_TALOFINT = HOLLY_INTC_ERR | 0x8,
  // Illegal Parameter Input
  HOLLY_INTC_TAIPINT = HOLLY_INTC_ERR | 0x10,
  // TA FIFO Over Flow
  HOLLY_INTC_TAFOFINT = HOLLY_INTC_ERR | 0x20,
  // PVR Illegal Address Set
  HOLLY_INTC_PIIAINT = HOLLY_INTC_ERR | 0x40,
  // PVR DMA Over Run
  HOLLY_INTC_PIORINT = HOLLY_INTC_ERR | 0x80,
  // MAPLE Illegal Address Set
  HOLLY_INTC_MIAINT = HOLLY_INTC_ERR | 0x100,
  // MAPLE DMA Over Run
  HOLLY_INTC_MORINT = HOLLY_INTC_ERR | 0x200,
  // MAPLE Write FIFO Overf Flow
  HOLLY_INTC_MFOFINT = HOLLY_INTC_ERR | 0x400,
  // MAPLE Illegal Command
  HOLLY_INTC_MICINT = HOLLY_INTC_ERR | 0x800,
  // G1 Illegal Address Set
  HOLLY_INTC_G1IAINT = HOLLY_INTC_ERR | 0x1000,
  // G1 DMA Over Run
  HOLLY_INTC_G1ORINT = HOLLY_INTC_ERR | 0x2000,
  // G1 Access at DMA
  HOLLY_INTC_G1ATINT = HOLLY_INTC_ERR | 0x4000,
  // G2 AICA-DMA Illegal Address Set
  HOLLY_INTC_G2IAAINT = HOLLY_INTC_ERR | 0x8000,
  // G2 Ext1-DMA Illegal Address Set
  HOLLY_INTC_G2IA1INT = HOLLY_INTC_ERR | 0x10000,
  // G2 Ext2-DMA Illegal Address Set
  HOLLY_INTC_G2IA2INT = HOLLY_INTC_ERR | 0x20000,
  // G2 Dev-DMA Illegal Address Set
  HOLLY_INTC_G2IADINT = HOLLY_INTC_ERR | 0x40000,
  // G2 AICA-DMA Over Run
  HOLLY_INTC_G2ORAINT = HOLLY_INTC_ERR | 0x80000,
  // G2 Ext1-DMA Over Run
  HOLLY_INTC_G2OR1INT = HOLLY_INTC_ERR | 0x100000,
  // G2 Ext2-DMA Over Run
  HOLLY_INTC_G2OR2INT = HOLLY_INTC_ERR | 0x200000,
  // G2 Dev-DMA Over Run
  HOLLY_INTC_G2ORDINT = HOLLY_INTC_ERR | 0x400000,
  // G2 AICA-DMA Time Out
  HOLLY_INTC_G2TOAINT = HOLLY_INTC_ERR | 0x800000,
  // G2 Ext1-DMA Time Out
  HOLLY_INTC_G2TO1INT = HOLLY_INTC_ERR | 0x1000000,
  // G2 Ext2-DMA Time Out
  HOLLY_INTC_G2TO2INT = HOLLY_INTC_ERR | 0x2000000,
  // G2 Dev-DMA Time Out
  HOLLY_INTC_G2TODINT = HOLLY_INTC_ERR | 0x4000000,
  // G2 Time Out in CPU Accessing
  HOLLY_INTC_G2TOCINT = HOLLY_INTC_ERR | 0x8000000,
  // Sort-DMA Command Error
  HOLLY_INTC_DTCESINT = HOLLY_INTC_ERR | 0x10000000,
  HOLLY_INTC_RESERVED1 = HOLLY_INTC_ERR | 0x20000000,
  HOLLY_INTC_RESERVED2 = HOLLY_INTC_ERR | 0x40000000,
  // SH4 Accessing to Inhibited Area
  HOLLY_INTC_CIHINT = HOLLY_INTC_ERR | 0x80000000
};

class Holly {
 public:
  Holly(hw::Dreamcast *dc);

  bool Init();

  void RequestInterrupt(HollyInterrupt intr);
  void UnrequestInterrupt(HollyInterrupt intr);

  uint32_t ReadRegister32(uint32_t addr);
  void WriteRegister32(uint32_t addr, uint32_t value);

 private:
  void CH2DMATransfer();
  void SortDMATransfer();
  void ForwardRequestInterrupts();

  hw::Dreamcast *dc_;
  hw::Register *holly_regs_;
  hw::sh4::SH4 *sh4_;
};
}
}
}

#endif
