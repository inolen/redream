#ifndef SH4_DMAC_H
#define SH4_DMAC_H

enum {
  SH4_DMA_FROM_ADDR,
  SH4_DMA_TO_ADDR,
};

struct sh4_dtr {
  int channel;
  int dir;
  /* when data is non-null, a single address mode transfer is performed between
     the external device memory at data, and the memory at addr

     when data is null, a dual address mode transfer is performed between addr
     and SARn / DARn */
  uint8_t *data;
  uint32_t addr;
  /* size is only valid for single address mode transfers, dual address mode
     transfers honor DMATCR */
  int size;
};

void sh4_dmac_ddt(struct sh4 *sh, struct sh4_dtr *dtr);

#endif
