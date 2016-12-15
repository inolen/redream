#include "hw/holly/holly.h"
#include "hw/dreamcast.h"
#include "hw/gdrom/gdrom.h"
#include "hw/maple/maple.h"
#include "hw/sh4/sh4.h"

struct reg_cb holly_cb[NUM_HOLLY_REGS];

static void holly_ch2_dma(struct holly *hl) {
  /* FIXME what are SB_LMMODE0 / SB_LMMODE1 */
  struct sh4_dtr dtr = {0};
  dtr.channel = 2;
  dtr.rw = 0;
  dtr.addr = *hl->SB_C2DSTAT;
  sh4_dmac_ddt(hl->sh4, &dtr);

  *hl->SB_C2DLEN = 0;
  *hl->SB_C2DST = 0;
  holly_raise_interrupt(hl, HOLLY_INTC_DTDE2INT);
}

static void holly_gdrom_dma(struct holly *hl) {
  if (!*hl->SB_GDEN) {
    return;
  }

  struct gdrom *gd = hl->gdrom;
  struct sh4 *sh4 = hl->sh4;

  /* only gdrom -> sh4 supported for now */
  CHECK_EQ(*hl->SB_GDDIR, 1);

  int transfer_size = *hl->SB_GDLEN;
  int remaining = transfer_size;
  uint32_t addr = *hl->SB_GDSTAR;

  gdrom_dma_begin(gd);

  while (remaining) {
    /* read a single sector at a time from the gdrom */
    uint8_t sector_data[SECTOR_SIZE];
    int n = gdrom_dma_read(gd, sector_data, sizeof(sector_data));

    struct sh4_dtr dtr = {0};
    dtr.channel = 0;
    dtr.rw = 1;
    dtr.data = sector_data;
    dtr.addr = addr;
    dtr.size = n;
    sh4_dmac_ddt(sh4, &dtr);

    remaining -= n;
    addr += n;
  }

  gdrom_dma_end(gd);

  *hl->SB_GDSTARD = addr;
  *hl->SB_GDLEND = transfer_size;
  *hl->SB_GDST = 0;
  holly_raise_interrupt(hl, HOLLY_INTC_G1DEINT);
}

static void holly_g2_dma(struct holly *hl, int channel) {
  /* clang-format off */
  struct g2_channel_desc {
    int STAG, STAR, LEN, DIR, TSEL, EN, ST, SUSP;
    holly_interrupt_t INTR;
  };
  static struct g2_channel_desc channel_descs[] = {
      {SB_ADSTAG, SB_ADSTAR, SB_ADLEN, SB_ADDIR, SB_ADTSEL, SB_ADEN, SB_ADST, SB_ADSUSP, HOLLY_INTC_G2DEAINT},
      {SB_E1STAG, SB_E1STAR, SB_E1LEN, SB_E1DIR, SB_E1TSEL, SB_E1EN, SB_E1ST, SB_E1SUSP, HOLLY_INTC_G2DE1INT},
      {SB_E2STAG, SB_E2STAR, SB_E2LEN, SB_E2DIR, SB_E2TSEL, SB_E2EN, SB_E2ST, SB_E2SUSP, HOLLY_INTC_G2DE2INT},
      {SB_DDSTAG, SB_DDSTAR, SB_DDLEN, SB_DDDIR, SB_DDTSEL, SB_DDEN, SB_DDST, SB_DDSUSP, HOLLY_INTC_G2DEDINT},
  };
  /* clang-format on */

  struct g2_channel_desc *desc = &channel_descs[channel];
  uint32_t *STAG = &hl->reg[desc->STAG];
  uint32_t *STAR = &hl->reg[desc->STAR];
  uint32_t *LEN = &hl->reg[desc->LEN];
  uint32_t *DIR = &hl->reg[desc->DIR];
  uint32_t *TSEL = &hl->reg[desc->TSEL];
  uint32_t *EN = &hl->reg[desc->EN];
  uint32_t *ST = &hl->reg[desc->ST];
  uint32_t *SUSP = &hl->reg[desc->SUSP];
  holly_interrupt_t INTR = desc->INTR;

  if (!*EN) {
    return;
  }

  struct address_space *space = hl->sh4->memory_if->space;
  int transfer_size = *LEN & 0x7fffffff;
  int enable = *LEN >> 31;
  int remaining = transfer_size;
  uint32_t src = *STAR;
  uint32_t dst = *STAG;

  /* only sh4 -> g2 supported for now */
  CHECK_EQ(*DIR, 0);

  while (remaining) {
    as_write32(space, dst, as_read32(space, src));
    remaining -= 4;
    src += 4;
    dst += 4;
  }

  *STAR = src;
  *STAG = dst;
  *LEN = 0;
  *EN = enable ? 1 : *EN;
  *ST = 0;
  holly_raise_interrupt(hl, INTR);
}

static void holly_maple_dma(struct holly *hl) {
  if (!*hl->SB_MDEN) {
    return;
  }

  struct maple *mp = hl->maple;
  struct address_space *space = hl->sh4->memory_if->space;
  uint32_t addr = *hl->SB_MDSTAR;

  while (1) {
    union maple_transfer desc;
    desc.full = as_read32(space, addr);
    addr += 4;

    switch (desc.pattern) {
      case MAPLE_PATTERN_NORMAL: {
        uint32_t result_addr = as_read32(space, addr);
        addr += 4;

        /* read message */
        struct maple_frame frame, res;
        frame.header.full = as_read32(space, addr);
        addr += 4;

        for (uint32_t i = 0; i < frame.header.num_words; i++) {
          frame.params[i] = as_read32(space, addr);
          addr += 4;
        }

        /* process message */
        int handled = maple_handle_command(mp, &frame, &res);

        /* write response */
        if (handled) {
          as_write32(space, result_addr, res.header.full);
          result_addr += 4;

          for (uint32_t i = 0; i < res.header.num_words; i++) {
            as_write32(space, result_addr, res.params[i]);
            result_addr += 4;
          }
        } else {
          as_write32(space, result_addr, 0xffffffff);
        }
      } break;

      case MAPLE_PATTERN_NOP:
        break;

      default:
        LOG_FATAL("Unhandled maple pattern 0x%x", desc.pattern);
        break;
    }

    if (desc.last) {
      break;
    }
  }

  *hl->SB_MDST = 0;
  holly_raise_interrupt(hl, HOLLY_INTC_MDEINT);
}

static void holly_update_interrupts(struct holly *hl) {
  /* trigger the respective level-encoded interrupt on the sh4 interrupt
     controller */
  {
    if ((*hl->SB_ISTNRM & *hl->SB_IML6NRM) ||
        (*hl->SB_ISTERR & *hl->SB_IML6ERR) ||
        (*hl->SB_ISTEXT & *hl->SB_IML6EXT)) {
      sh4_raise_interrupt(hl->sh4, SH4_INTC_IRL_9);
    } else {
      sh4_clear_interrupt(hl->sh4, SH4_INTC_IRL_9);
    }
  }

  {
    if ((*hl->SB_ISTNRM & *hl->SB_IML4NRM) ||
        (*hl->SB_ISTERR & *hl->SB_IML4ERR) ||
        (*hl->SB_ISTEXT & *hl->SB_IML4EXT)) {
      sh4_raise_interrupt(hl->sh4, SH4_INTC_IRL_11);
    } else {
      sh4_clear_interrupt(hl->sh4, SH4_INTC_IRL_11);
    }
  }

  {
    if ((*hl->SB_ISTNRM & *hl->SB_IML2NRM) ||
        (*hl->SB_ISTERR & *hl->SB_IML2ERR) ||
        (*hl->SB_ISTEXT & *hl->SB_IML2EXT)) {
      sh4_raise_interrupt(hl->sh4, SH4_INTC_IRL_13);
    } else {
      sh4_clear_interrupt(hl->sh4, SH4_INTC_IRL_13);
    }
  }
}

static void holly_reg_write(struct holly *hl, uint32_t addr, uint32_t data,
                            uint32_t data_mask) {
  uint32_t offset = addr >> 2;
  reg_write_cb write = holly_cb[offset].write;
  if (write) {
    write(hl->dc, data);
    return;
  }
  hl->reg[offset] = data;
}

static uint32_t holly_reg_read(struct holly *hl, uint32_t addr,
                               uint32_t data_mask) {
  uint32_t offset = addr >> 2;
  reg_read_cb read = holly_cb[offset].read;
  if (read) {
    return read(hl->dc);
  }
  return hl->reg[offset];
}

static uint32_t *holly_interrupt_status(struct holly *hl,
                                        enum holly_interrupt_type type) {
  switch (type) {
    case HOLLY_INTC_NRM:
      return hl->SB_ISTNRM;
    case HOLLY_INTC_EXT:
      return hl->SB_ISTEXT;
    case HOLLY_INTC_ERR:
      return hl->SB_ISTERR;
    default:
      LOG_FATAL("Invalid interrupt type");
  }
}

void holly_clear_interrupt(struct holly *hl, holly_interrupt_t intr) {
  enum holly_interrupt_type type = HOLLY_INTERRUPT_TYPE(intr);
  uint32_t irq = HOLLY_INTERRUPT_IRQ(intr);

  uint32_t *status = holly_interrupt_status(hl, type);
  *status &= ~irq;

  holly_update_interrupts(hl);
}

void holly_raise_interrupt(struct holly *hl, holly_interrupt_t intr) {
  enum holly_interrupt_type type = HOLLY_INTERRUPT_TYPE(intr);
  uint32_t irq = HOLLY_INTERRUPT_IRQ(intr);

  uint32_t *status = holly_interrupt_status(hl, type);
  *status |= irq;

  holly_update_interrupts(hl);

  /* check for hardware dma initiation */
  if (intr == HOLLY_INTC_PCVOINT && *hl->SB_MDTSEL && *hl->SB_MDEN) {
    holly_maple_dma(hl);
  }
}

static bool holly_init(struct device *dev) {
  struct holly *hl = (struct holly *)dev;
  return true;
}

void holly_destroy(struct holly *hl) {
  dc_destroy_device((struct device *)hl);
}

struct holly *holly_create(struct dreamcast *dc) {
  struct holly *hl =
      dc_create_device(dc, sizeof(struct holly), "holly", &holly_init);

/* init registers */
#define HOLLY_REG(addr, name, default, type) \
  hl->reg[name] = default;                   \
  hl->name = (type *)&hl->reg[name];
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG

  return hl;
}

REG_R32(holly_cb, SB_ISTNRM) {
  struct holly *hl = dc->holly;
  /* note that the two highest bits indicate the OR'ed result of all of the
     bits in SB_ISTEXT and SB_ISTERR, respectively, and writes to these two
     bits are ignored */
  uint32_t v = *hl->SB_ISTNRM & 0x3fffffff;
  if (*hl->SB_ISTEXT) {
    v |= 0x40000000;
  }
  if (*hl->SB_ISTERR) {
    v |= 0x80000000;
  }
  return v;
}

REG_W32(holly_cb, SB_ISTNRM) {
  struct holly *hl = dc->holly;
  /* writing a 1 clears the interrupt */
  *hl->SB_ISTNRM &= ~value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_ISTEXT) {
  struct holly *hl = dc->holly;
  *hl->SB_ISTEXT &= ~value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_ISTERR) {
  struct holly *hl = dc->holly;
  *hl->SB_ISTERR &= ~value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_IML2NRM) {
  struct holly *hl = dc->holly;
  *hl->SB_IML2NRM = value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_IML2EXT) {
  struct holly *hl = dc->holly;
  *hl->SB_IML2EXT = value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_IML2ERR) {
  struct holly *hl = dc->holly;
  *hl->SB_IML2ERR = value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_IML4NRM) {
  struct holly *hl = dc->holly;
  *hl->SB_IML4NRM = value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_IML4EXT) {
  struct holly *hl = dc->holly;
  *hl->SB_IML4EXT = value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_IML4ERR) {
  struct holly *hl = dc->holly;
  *hl->SB_IML4ERR = value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_IML6NRM) {
  struct holly *hl = dc->holly;
  *hl->SB_IML6NRM = value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_IML6EXT) {
  struct holly *hl = dc->holly;
  *hl->SB_IML6EXT = value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_IML6ERR) {
  struct holly *hl = dc->holly;
  *hl->SB_IML6ERR = value;
  holly_update_interrupts(hl);
}

REG_W32(holly_cb, SB_C2DST) {
  struct holly *hl = dc->holly;
  if ((*hl->SB_C2DST = value)) {
    holly_ch2_dma(hl);
  }
}

REG_W32(holly_cb, SB_SDST) {
  struct holly *hl = dc->holly;
  if ((*hl->SB_SDST = value)) {
    LOG_FATAL("Sort DMA not supported");
  }
}

REG_W32(holly_cb, SB_MDST) {
  struct holly *hl = dc->holly;
  if ((*hl->SB_MDST = value)) {
    holly_maple_dma(hl);
  }
}

REG_W32(holly_cb, SB_GDST) {
  struct holly *hl = dc->holly;
  if ((*hl->SB_GDST = value)) {
    holly_gdrom_dma(hl);
  }
}

REG_W32(holly_cb, SB_ADST) {
  struct holly *hl = dc->holly;
  if ((*hl->SB_ADST = value)) {
    holly_g2_dma(hl, 0);
  }
}

REG_W32(holly_cb, SB_ADTSEL) {
  if ((value & 0x2) == 0x2) {
    LOG_FATAL("Hardware DMA trigger not supported");
  }
}

REG_W32(holly_cb, SB_E1ST) {
  struct holly *hl = dc->holly;
  if ((*hl->SB_E1ST = value)) {
    holly_g2_dma(hl, 1);
  }
}

REG_W32(holly_cb, SB_E1TSEL) {
  if ((value & 0x2) == 0x2) {
    LOG_FATAL("Hardware DMA trigger not supported");
  }
}

REG_W32(holly_cb, SB_E2ST) {
  struct holly *hl = dc->holly;
  if ((*hl->SB_E2ST = value)) {
    holly_g2_dma(hl, 2);
  }
}

REG_W32(holly_cb, SB_E2TSEL) {
  if ((value & 0x2) == 0x2) {
    LOG_FATAL("Hardware DMA trigger not supported");
  }
}

REG_W32(holly_cb, SB_DDST) {
  struct holly *hl = dc->holly;
  if ((*hl->SB_DDST = value)) {
    holly_g2_dma(hl, 3);
  }
}

REG_W32(holly_cb, SB_DDTSEL) {
  if ((value & 0x2) == 0x2) {
    LOG_FATAL("Hardware DMA trigger not supported");
  }
}

REG_W32(holly_cb, SB_PDST) {
  struct holly *hl = dc->holly;
  if ((*hl->SB_PDST = value)) {
    LOG_FATAL("PVR DMA not supported");
  }
}

REG_W32(holly_cb, SB_PDTSEL) {
  if (value) {
    LOG_FATAL("Hardware DMA trigger not supported");
  }
}

/* clang-format off */
AM_BEGIN(struct holly, holly_reg_map);
  /* over-allocate to align with the host allocation granularity */
  AM_RANGE(0x00000000, 0x00007fff) AM_HANDLE("holly reg",
                                             (mmio_read_cb)&holly_reg_read,
                                             (mmio_write_cb)&holly_reg_write,
                                             NULL, NULL)
AM_END();

AM_BEGIN(struct holly, holly_modem_map);
  AM_RANGE(0x00000000, 0x0007ffff) AM_MOUNT("modem reg")
AM_END();

AM_BEGIN(struct holly, holly_expansion0_map);
  AM_RANGE(0x00000000, 0x00ffffff) AM_MOUNT("expansion 0")
AM_END();

AM_BEGIN(struct holly, holly_expansion1_map);
  AM_RANGE(0x00000000, 0x008fffff) AM_MOUNT("expansion 1")
AM_END();

AM_BEGIN(struct holly, holly_expansion2_map);
  AM_RANGE(0x00000000, 0x03ffffff) AM_MOUNT("expansion 2")
AM_END();
/* clang-format on */
