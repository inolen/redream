#include "guest/holly/holly.h"
#include "guest/dreamcast.h"
#include "guest/gdrom/gdrom.h"
#include "guest/maple/maple.h"
#include "guest/scheduler.h"
#include "guest/sh4/sh4.h"
#include "render/imgui.h"

struct reg_cb holly_cb[NUM_HOLLY_REGS];

/*
 * ch2 dma
 */
static void holly_ch2_dma_stop(struct holly *hl) {
  /* nop as DMA is always performed synchronously */
}

static void holly_ch2_dma(struct holly *hl) {
  /* FIXME what are SB_LMMODE0 / SB_LMMODE1 */
  struct sh4_dtr dtr = {0};
  dtr.channel = 2;
  dtr.dir = SH4_DMA_TO_ADDR;
  dtr.addr = *hl->SB_C2DSTAT;
  sh4_dmac_ddt(hl->sh4, &dtr);

  *hl->SB_C2DLEN = 0;
  *hl->SB_C2DST = 0;
  holly_raise_interrupt(hl, HOLLY_INT_DTDE2INT);
}

/*
 * gdrom dma
 */
static void holly_gdrom_dma(struct holly *hl) {
  if (!*hl->SB_GDEN) {
    *hl->SB_GDST = 0;
    return;
  }

  struct gdrom *gd = hl->gdrom;
  struct sh4 *sh4 = hl->sh4;

  /* only gdrom -> sh4 supported for now */
  CHECK_EQ(*hl->SB_GDDIR, 1);

  int transfer_size = *hl->SB_GDLEN;
  int remaining = transfer_size;
  uint32_t addr = *hl->SB_GDSTAR;
  uint8_t sector_data[DISC_MAX_SECTOR_SIZE];

  gdrom_dma_begin(gd);

  /* TODO give callback to ddt interface to call instead of data? */

  while (remaining) {
    /* read a single sector at a time from the gdrom */
    int n = MIN(remaining, (int)sizeof(sector_data));
    n = gdrom_dma_read(gd, sector_data, n);

    struct sh4_dtr dtr = {0};
    dtr.channel = 0;
    dtr.dir = SH4_DMA_TO_ADDR;
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
  holly_raise_interrupt(hl, HOLLY_INT_G1DEINT);
}

/*
 * maple dma
 */
static void holly_maple_dma(struct holly *hl) {
  if (!*hl->SB_MDEN) {
    *hl->SB_MDST = 0;
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
        LOG_FATAL("unhandled maple pattern 0x%x", desc.pattern);
        break;
    }

    if (desc.last) {
      break;
    }
  }

  *hl->SB_MDST = 0;
  holly_raise_interrupt(hl, HOLLY_INT_MDEINT);
}

/*
 * g2 dma
 */

/* clang-format off */
struct g2_channel_desc {
  int STAG, STAR, LEN, DIR, TSEL, EN, ST, SUSP;
  holly_interrupt_t INTR;
};

static struct g2_channel_desc g2_channels[] = {
    {SB_ADSTAG, SB_ADSTAR, SB_ADLEN, SB_ADDIR, SB_ADTSEL, SB_ADEN, SB_ADST, SB_ADSUSP, HOLLY_INT_G2DEAINT},
    {SB_E1STAG, SB_E1STAR, SB_E1LEN, SB_E1DIR, SB_E1TSEL, SB_E1EN, SB_E1ST, SB_E1SUSP, HOLLY_INT_G2DE1INT},
    {SB_E2STAG, SB_E2STAR, SB_E2LEN, SB_E2DIR, SB_E2TSEL, SB_E2EN, SB_E2ST, SB_E2SUSP, HOLLY_INT_G2DE2INT},
    {SB_DDSTAG, SB_DDSTAR, SB_DDLEN, SB_DDDIR, SB_DDTSEL, SB_DDEN, SB_DDST, SB_DDSUSP, HOLLY_INT_G2DEDINT},
};

#define DEFINE_G2_DMA_TIMER(channel)                       \
  static void holly_g2_dma_timer_ch##channel(void *data) { \
    struct holly *hl = data;                               \
    struct g2_channel_desc *desc = &g2_channels[channel];  \
    holly_raise_interrupt(hl, desc->INTR);                 \
  }

DEFINE_G2_DMA_TIMER(0);
DEFINE_G2_DMA_TIMER(1);
DEFINE_G2_DMA_TIMER(2);
DEFINE_G2_DMA_TIMER(3);

static void (*g2_timers[])(void *) = {
  &holly_g2_dma_timer_ch0,
  &holly_g2_dma_timer_ch1,
  &holly_g2_dma_timer_ch2,
  &holly_g2_dma_timer_ch3,
};
/* clang-format on */

static void holly_g2_dma_suspend(struct holly *hl, int channel) {
  struct g2_channel_desc *desc = &g2_channels[channel];

  if (!hl->reg[desc->EN] || !hl->reg[desc->ST]) {
    return;
  }

  LOG_FATAL("holly_g2_dma_suspend not supported");
}

static void holly_g2_dma(struct holly *hl, int channel) {
  struct g2_channel_desc *desc = &g2_channels[channel];

  if (!hl->reg[desc->EN]) {
    hl->reg[desc->ST] = 0;
    return;
  }

  struct address_space *space = hl->sh4->memory_if->space;
  uint32_t len = hl->reg[desc->LEN];
  int restart = (len >> 31) == 0;
  int transfer_size = len & 0x7fffffff;
  int remaining = transfer_size;
  uint32_t src = hl->reg[desc->STAR];
  uint32_t dst = hl->reg[desc->STAG];

  /* only sh4 -> g2 supported for now */
  CHECK_EQ(hl->reg[desc->DIR], 0);

  /* perform the DMA immediately, but don't raise the end of DMA interrupt until
     the DMA should actually end. this hopefully fixes issues in games which
     break when DMAs end immediately, without having to actually emulate the
     16-bit x 25mhz g2 bus transfer */
  while (remaining) {
    as_write32(space, dst, as_read32(space, src));
    remaining -= 4;
    src += 4;
    dst += 4;
  }

  /* the status registers need to be updated immediately as well. if they're not
     updated until the interrupt is raised, the DMA functions used by games will
     try to suspend the transfer due to a lack of progress */
  hl->reg[desc->LEN] = 0;
  hl->reg[desc->EN] = restart;
  hl->reg[desc->ST] = 0;

  int64_t end = CYCLES_TO_NANO(transfer_size / 2, UINT64_C(25000000));
  scheduler_start_timer(hl->scheduler, g2_timers[channel], hl, end);
}

static void holly_update_interrupts(struct holly *hl) {
  /* trigger the respective level-encoded interrupt on the sh4 interrupt
     controller */
  {
    if ((*hl->SB_ISTNRM & *hl->SB_IML6NRM) ||
        (*hl->SB_ISTERR & *hl->SB_IML6ERR) ||
        (*hl->SB_ISTEXT & *hl->SB_IML6EXT)) {
      sh4_raise_interrupt(hl->sh4, SH4_INT_IRL_9);
    } else {
      sh4_clear_interrupt(hl->sh4, SH4_INT_IRL_9);
    }
  }

  {
    if ((*hl->SB_ISTNRM & *hl->SB_IML4NRM) ||
        (*hl->SB_ISTERR & *hl->SB_IML4ERR) ||
        (*hl->SB_ISTEXT & *hl->SB_IML4EXT)) {
      sh4_raise_interrupt(hl->sh4, SH4_INT_IRL_11);
    } else {
      sh4_clear_interrupt(hl->sh4, SH4_INT_IRL_11);
    }
  }

  {
    if ((*hl->SB_ISTNRM & *hl->SB_IML2NRM) ||
        (*hl->SB_ISTERR & *hl->SB_IML2ERR) ||
        (*hl->SB_ISTEXT & *hl->SB_IML2EXT)) {
      sh4_raise_interrupt(hl->sh4, SH4_INT_IRL_13);
    } else {
      sh4_clear_interrupt(hl->sh4, SH4_INT_IRL_13);
    }
  }
}

static void holly_reg_write(struct holly *hl, uint32_t addr, uint32_t data,
                            uint32_t data_mask) {
  uint32_t offset = addr >> 2;
  reg_write_cb write = holly_cb[offset].write;

  if (hl->log_reg_access) {
    LOG_INFO("holly_reg_write addr=0x%08x data=0x%x", addr, data & data_mask);
  }

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

  uint32_t data;
  if (read) {
    data = read(hl->dc);
  } else {
    data = hl->reg[offset];
  }

  if (hl->log_reg_access) {
    LOG_INFO("holly_reg_read addr=0x%08x data=0x%x", addr, data);
  }

  return data;
}

static uint32_t *holly_interrupt_status(struct holly *hl,
                                        enum holly_interrupt_type type) {
  switch (type) {
    case HOLLY_INT_NRM:
      return hl->SB_ISTNRM;
    case HOLLY_INT_EXT:
      return hl->SB_ISTEXT;
    case HOLLY_INT_ERR:
      return hl->SB_ISTERR;
    default:
      LOG_FATAL("invalid interrupt type");
  }
}

static int holly_init(struct device *dev) {
  struct holly *hl = (struct holly *)dev;
  return 1;
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
  if (intr == HOLLY_INT_PCVOINT && *hl->SB_MDTSEL && *hl->SB_MDEN) {
    holly_maple_dma(hl);
  }
}

#if ENABLE_IMGUI
void holly_debug_menu(struct holly *hl) {
  if (igBeginMainMenuBar()) {
    if (igBeginMenu("HOLLY", 1)) {
      if (igMenuItem("log reg access", NULL, hl->log_reg_access, 1)) {
        hl->log_reg_access = !hl->log_reg_access;
      }

      if (igMenuItem("raise all HOLLY_INT_NRM", NULL, 0, 1)) {
        for (int i = 0; i < 22; i++) {
          holly_raise_interrupt(hl, HOLLY_INTERRUPT(HOLLY_INT_NRM, 1 << i));
        }
      }
      if (igMenuItem("clear all HOLLY_INT_NRM", NULL, 0, 1)) {
        for (int i = 0; i < 22; i++) {
          holly_clear_interrupt(hl, HOLLY_INTERRUPT(HOLLY_INT_NRM, 1 << i));
        }
      }

      if (igMenuItem("raise all HOLLY_INT_EXT", NULL, 0, 1)) {
        for (int i = 0; i < 4; i++) {
          holly_raise_interrupt(hl, HOLLY_INTERRUPT(HOLLY_INT_EXT, 1 << i));
        }
      }
      if (igMenuItem("clear all HOLLY_INT_EXT", NULL, 0, 1)) {
        for (int i = 0; i < 4; i++) {
          holly_clear_interrupt(hl, HOLLY_INTERRUPT(HOLLY_INT_EXT, 1 << i));
        }
      }

      igEndMenu();
    }

    igEndMainMenuBar();
  }
}
#endif

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
#include "guest/holly/holly_regs.inc"
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
  /* this register is used to confirm external interrupts. these interrupts can
     only be cancelled by the external device itself, they cannot be cancelled
     through this register */
}

REG_W32(holly_cb, SB_ISTERR) {
  struct holly *hl = dc->holly;
  /* writing a 1 clears the interrupt */
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

  *hl->SB_C2DST = value;

  if (*hl->SB_C2DST) {
    holly_ch2_dma(hl);
  } else {
    holly_ch2_dma_stop(hl);
  }
}

REG_W32(holly_cb, SB_SDST) {
  struct holly *hl = dc->holly;

  /* can't write 0 */
  *hl->SB_SDST |= value;

  if (*hl->SB_SDST) {
    LOG_FATAL("sort DMA not supported");
  }
}

REG_W32(holly_cb, SB_MDST) {
  struct holly *hl = dc->holly;

  /* can't write 0 */
  *hl->SB_MDST |= value;

  if (*hl->SB_MDST) {
    holly_maple_dma(hl);
  }
}

REG_W32(holly_cb, SB_GDST) {
  struct holly *hl = dc->holly;

  /* can't write 0 */
  *hl->SB_GDST |= value;

  if (*hl->SB_GDST) {
    holly_gdrom_dma(hl);
  }
}

REG_W32(holly_cb, SB_ADST) {
  struct holly *hl = dc->holly;

  /* can't write 0 */
  *hl->SB_ADST |= value;

  if (*hl->SB_ADST) {
    holly_g2_dma(hl, 0);
  }
}

REG_W32(holly_cb, SB_ADSUSP) {
  struct holly *hl = dc->holly;

  hl->SB_ADSUSP->full = value;

  if (hl->SB_ADTSEL->susp && hl->SB_ADSUSP->susp) {
    holly_g2_dma_suspend(hl, 0);
  }
}

REG_W32(holly_cb, SB_ADTSEL) {
  struct holly *hl = dc->holly;

  hl->SB_ADTSEL->full = value;

  if (hl->SB_ADTSEL->hw) {
    LOG_FATAL("hardware DMA trigger not supported");
  }
}

REG_W32(holly_cb, SB_E1ST) {
  struct holly *hl = dc->holly;

  /* can't write 0 */
  *hl->SB_E1ST |= value;

  if (*hl->SB_E1ST) {
    holly_g2_dma(hl, 1);
  }
}

REG_W32(holly_cb, SB_E1SUSP) {
  struct holly *hl = dc->holly;

  hl->SB_E1SUSP->full = value;

  if (hl->SB_E1TSEL->susp && hl->SB_E1SUSP->susp) {
    holly_g2_dma_suspend(hl, 1);
  }
}

REG_W32(holly_cb, SB_E1TSEL) {
  struct holly *hl = dc->holly;

  hl->SB_E1TSEL->full = value;

  if (hl->SB_E1TSEL->hw) {
    LOG_FATAL("hardware DMA trigger not supported");
  }
}

REG_W32(holly_cb, SB_E2ST) {
  struct holly *hl = dc->holly;

  /* can't write 0 */
  *hl->SB_E2ST |= value;

  if (*hl->SB_E2ST) {
    holly_g2_dma(hl, 2);
  }
}

REG_W32(holly_cb, SB_E2SUSP) {
  struct holly *hl = dc->holly;

  hl->SB_E2SUSP->full = value;

  if (hl->SB_E2TSEL->susp && hl->SB_E2SUSP->susp) {
    holly_g2_dma_suspend(hl, 2);
  }
}

REG_W32(holly_cb, SB_E2TSEL) {
  struct holly *hl = dc->holly;

  hl->SB_E2TSEL->full = value;

  if (hl->SB_E2TSEL->hw) {
    LOG_FATAL("hardware DMA trigger not supported");
  }
}

REG_W32(holly_cb, SB_DDST) {
  struct holly *hl = dc->holly;

  /* can't write 0 */
  *hl->SB_DDST |= value;

  if (*hl->SB_DDST) {
    holly_g2_dma(hl, 3);
  }
}

REG_W32(holly_cb, SB_DDSUSP) {
  struct holly *hl = dc->holly;

  hl->SB_DDSUSP->full = value;

  if (hl->SB_DDTSEL->susp && hl->SB_DDSUSP->susp) {
    holly_g2_dma_suspend(hl, 3);
  }
}

REG_W32(holly_cb, SB_DDTSEL) {
  struct holly *hl = dc->holly;

  hl->SB_DDTSEL->full = value;

  if (hl->SB_DDTSEL->hw) {
    LOG_FATAL("hardware DMA trigger not supported");
  }
}

REG_W32(holly_cb, SB_PDST) {
  struct holly *hl = dc->holly;

  /* can't write 0 */
  *hl->SB_PDST |= value;

  if (*hl->SB_PDST) {
    LOG_FATAL("pvr DMA not supported");
  }
}

REG_W32(holly_cb, SB_PDTSEL) {
  struct holly *hl = dc->holly;

  *hl->SB_PDTSEL = value;

  if (*hl->SB_PDTSEL) {
    LOG_FATAL("hardware DMA trigger not supported");
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
