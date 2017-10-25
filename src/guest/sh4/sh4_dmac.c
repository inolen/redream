#include "guest/memory.h"
#include "guest/sh4/sh4.h"

static void sh4_dmac_check(struct sh4 *sh4, int channel) {
  union chcr *chcr = NULL;

  switch (channel) {
    case 0:
      chcr = sh4->CHCR0;
      break;
    case 1:
      chcr = sh4->CHCR1;
      break;
    case 2:
      chcr = sh4->CHCR2;
      break;
    case 3:
      chcr = sh4->CHCR3;
      break;
    default:
      LOG_FATAL("sh4_dmac_check unexpected channel %d", channel);
      break;
  }

  CHECK(sh4->DMAOR->DDT || !sh4->DMAOR->DME || !chcr->DE,
        "sh4_dmac_check only DDT DMA unsupported");
}

void sh4_dmac_ddt(struct sh4 *sh4, struct sh4_dtr *dtr) {
  struct memory *mem = sh4->dc->mem;

  /* FIXME this should be made asynchronous, at which point the significance
     of the registers / interrupts should be more obvious */

  if (dtr->data) {
    /* single address mode transfer */
    if (dtr->dir == SH4_DMA_FROM_ADDR) {
      sh4_memcpy_to_host(mem, dtr->data, dtr->addr, dtr->size);
    } else {
      sh4_memcpy_to_guest(mem, dtr->addr, dtr->data, dtr->size);
    }
  } else {
    /* dual address mode transfer */
    uint32_t *sar;
    uint32_t *dar;
    uint32_t *dmatcr;
    union chcr *chcr;
    enum sh4_interrupt dmte;

    switch (dtr->channel) {
      case 0:
        sar = sh4->SAR0;
        dar = sh4->DAR0;
        dmatcr = sh4->DMATCR0;
        chcr = sh4->CHCR0;
        dmte = SH4_INT_DMTE0;
        break;
      case 1:
        sar = sh4->SAR1;
        dar = sh4->DAR1;
        dmatcr = sh4->DMATCR1;
        chcr = sh4->CHCR1;
        dmte = SH4_INT_DMTE1;
        break;
      case 2:
        sar = sh4->SAR2;
        dar = sh4->DAR2;
        dmatcr = sh4->DMATCR2;
        chcr = sh4->CHCR2;
        dmte = SH4_INT_DMTE2;
        break;
      case 3:
        sar = sh4->SAR3;
        dar = sh4->DAR3;
        dmatcr = sh4->DMATCR3;
        chcr = sh4->CHCR3;
        dmte = SH4_INT_DMTE3;
        break;
      default:
        LOG_FATAL("Unexpected DMA channel");
        break;
    }

    uint32_t src = dtr->dir == SH4_DMA_FROM_ADDR ? dtr->addr : *sar;
    uint32_t dst = dtr->dir == SH4_DMA_FROM_ADDR ? *dar : dtr->addr;
    int size = *dmatcr * 32;
    sh4_memcpy(mem, dst, src, size);

    /* update src / addresses as well as remaining count */
    *sar = src + size;
    *dar = dst + size;
    *dmatcr = 0;

    /* signal transfer end */
    chcr->TE = 1;

    /* raise interrupt if requested */
    if (chcr->IE) {
      sh4_raise_interrupt(sh4, dmte);
    }
  }
}

REG_W32(sh4_cb, CHCR0) {
  struct sh4 *sh4 = dc->sh4;
  sh4->CHCR0->full = value;
  sh4_dmac_check(sh4, 0);
}

REG_W32(sh4_cb, CHCR1) {
  struct sh4 *sh4 = dc->sh4;
  sh4->CHCR1->full = value;
  sh4_dmac_check(sh4, 1);
}

REG_W32(sh4_cb, CHCR2) {
  struct sh4 *sh4 = dc->sh4;
  sh4->CHCR2->full = value;
  sh4_dmac_check(sh4, 2);
}

REG_W32(sh4_cb, CHCR3) {
  struct sh4 *sh4 = dc->sh4;
  sh4->CHCR3->full = value;
  sh4_dmac_check(sh4, 3);
}

REG_W32(sh4_cb, DMAOR) {
  struct sh4 *sh4 = dc->sh4;
  sh4->DMAOR->full = value;
  sh4_dmac_check(sh4, 0);
  sh4_dmac_check(sh4, 1);
  sh4_dmac_check(sh4, 2);
  sh4_dmac_check(sh4, 3);
}
