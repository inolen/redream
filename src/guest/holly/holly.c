#include "guest/holly/holly.h"
#include "guest/dreamcast.h"
#include "guest/gdrom/gdrom.h"
#include "guest/maple/maple.h"
#include "guest/memory.h"
#include "guest/scheduler.h"
#include "guest/sh4/sh4.h"
#include "imgui.h"

#if 0
#define LOG_HOLLY LOG_INFO
#else
#define LOG_HOLLY(...)
#endif

struct reg_cb holly_cb[NUM_HOLLY_REGS];

/*
 * ch2 dma
 */
static void holly_ch2_dma_stop(struct holly *hl) {
  /* nop as DMA is always performed synchronously */
}

static void holly_ch2_dma(struct holly *hl) {
  struct sh4 *sh4 = hl->dc->sh4;

  struct sh4_dtr dtr = {0};
  dtr.channel = 2;
  dtr.dir = SH4_DMA_TO_ADDR;
  dtr.addr = *hl->SB_C2DSTAT;
  sh4_dmac_ddt(sh4, &dtr);

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

  struct gdrom *gd = hl->dc->gdrom;
  struct sh4 *sh4 = hl->dc->sh4;

  /* only gdrom -> sh4 supported for now */
  CHECK_EQ(*hl->SB_GDDIR, 1);

  int transfer_size = *hl->SB_GDLEN;
  int remaining = transfer_size;
  uint32_t addr = *hl->SB_GDSTAR;
  uint8_t sector_data[DISC_MAX_SECTOR_SIZE];

  gdrom_dma_begin(gd);

  while (1) {
    /* read a single sector at a time from the gdrom */
    int n = MIN(remaining, (int)sizeof(sector_data));
    n = gdrom_dma_read(gd, sector_data, n);

    if (!n) {
      break;
    }

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

  struct memory *mem = hl->dc->mem;
  struct maple *mp = hl->dc->maple;
  uint32_t addr = *hl->SB_MDSTAR;

  while (1) {
    union maple_transfer desc;
    desc.full = sh4_read32(mem, addr);
    addr += 4;

    switch (desc.pattern) {
      case MAPLE_PATTERN_NORMAL: {
        uint32_t result_addr = sh4_read32(mem, addr);
        addr += 4;

        /* read frame */
        union maple_frame frame, res;

        for (int i = 0; i < (int)desc.length + 1; i++) {
          frame.data[i] = sh4_read32(mem, addr);
          addr += 4;
        }

        /* process frame and write response */
        int handled = maple_handle_frame(mp, desc.port, &frame, &res);

        if (handled) {
          for (int i = 0; i < (int)res.num_words + 1; i++) {
            sh4_write32(mem, result_addr, res.data[i]);
            result_addr += 4;
          }
        } else {
          sh4_write32(mem, result_addr, 0xffffffff);
        }
      } break;

      case MAPLE_PATTERN_NOP:
        break;

      default:
        LOG_FATAL("holly_maple_dma unhandled pattern 0x%x", desc.pattern);
        break;
    }

    if (desc.end) {
      break;
    }
  }

  *hl->SB_MDST = 0;
  holly_raise_interrupt(hl, HOLLY_INT_MDEINT);
}

/*
 * g2 dma
 */

#define SB_STAG(ch) (hl->SB_ADSTAG + ((ch)*HOLLY_G2_NUM_REGS))
#define SB_STAR(ch) (hl->SB_ADSTAR + ((ch)*HOLLY_G2_NUM_REGS))
#define SB_LEN(ch) (hl->SB_ADLEN + ((ch)*HOLLY_G2_NUM_REGS))
#define SB_DIR(ch) (hl->SB_ADDIR + ((ch)*HOLLY_G2_NUM_REGS))
#define SB_TSEL(ch) (hl->SB_ADTSEL + ((ch)*HOLLY_G2_NUM_REGS))
#define SB_EN(ch) (hl->SB_ADEN + ((ch)*HOLLY_G2_NUM_REGS))
#define SB_ST(ch) (hl->SB_ADST + ((ch)*HOLLY_G2_NUM_REGS))
#define SB_SUSP(ch) (hl->SB_ADSUSP + ((ch)*HOLLY_G2_NUM_REGS))
#define HOLLY_INT_G2INT(ch) HOLLY_INTERRUPT(HOLLY_INT_NRM, (0x8000 << ch))

static void (*g2_timers[4])(void *);

#define DEFINE_G2_DMA_TIMER(ch)                                       \
  static void holly_g2_dma_timer_channel##ch(void *data) {            \
    struct holly *hl = data;                                          \
    struct memory *mem = hl->dc->mem;                                 \
    struct scheduler *sched = hl->dc->sched;                          \
    struct holly_g2_dma *dma = &hl->dma[ch];                          \
    int chunk_size = 0x1000;                                          \
    int n = MIN(dma->len, chunk_size);                                \
    sh4_memcpy(mem, dma->dst, dma->src, n);                           \
    dma->dst += n;                                                    \
    dma->src += n;                                                    \
    dma->len -= n;                                                    \
    if (!dma->len) {                                                  \
      *SB_EN(ch) = dma->restart;                                      \
      *SB_ST(ch) = 0;                                                 \
      holly_raise_interrupt(hl, HOLLY_INT_G2INT(ch));                 \
      return;                                                         \
    }                                                                 \
    /* g2 bus runs at 16-bits x 25mhz, loosely simulate this */       \
    int64_t end = CYCLES_TO_NANO(chunk_size / 2, UINT64_C(25000000)); \
    sched_start_timer(sched, g2_timers[ch], hl, end);                 \
  }

DEFINE_G2_DMA_TIMER(0);
DEFINE_G2_DMA_TIMER(1);
DEFINE_G2_DMA_TIMER(2);
DEFINE_G2_DMA_TIMER(3);

static void (*g2_timers[4])(void *) = {
    &holly_g2_dma_timer_channel0, &holly_g2_dma_timer_channel1,
    &holly_g2_dma_timer_channel2, &holly_g2_dma_timer_channel3,
};

static void holly_g2_dma_suspend(struct holly *hl, int ch) {
  if (!*SB_EN(ch) || !*SB_ST(ch)) {
    return;
  }

  /* FIXME this occurs because the scheduler code isn't accurate for timers
     created in the middle of executing a time slice. ignoring them seems
     safe for now */
  LOG_HOLLY("holly_g2_dma_suspend ignored");
}

static void holly_g2_dma(struct holly *hl, int ch) {
  if (!*SB_EN(ch)) {
    *SB_ST(ch) = 0;
    return;
  }

  /* only sh4 -> g2 supported for now */
  CHECK_EQ(*SB_DIR(ch), 0);

  /* latch register state */
  struct holly_g2_dma *dma = &hl->dma[ch];
  dma->dst = *SB_STAG(ch);
  dma->src = *SB_STAR(ch);
  dma->restart = (*SB_LEN(ch) & 0x80000000) == 0;
  dma->len = (*SB_LEN(ch) & 0x7fffffff);

  LOG_HOLLY("holly_g2_dma dst=0x%08x src=0x%08x len=0x%08x", dma->dst, dma->src,
            dma->len);

  /* kick off async dma */
  g2_timers[ch](hl);
}

static void holly_update_interrupts(struct holly *hl) {
  struct sh4 *sh4 = hl->dc->sh4;

  /* trigger the respective level-encoded interrupt on the sh4 interrupt
     controller */
  {
    if ((*hl->SB_ISTNRM & *hl->SB_IML6NRM) ||
        (*hl->SB_ISTERR & *hl->SB_IML6ERR) ||
        (*hl->SB_ISTEXT & *hl->SB_IML6EXT)) {
      sh4_raise_interrupt(sh4, SH4_INT_IRL_9);
    } else {
      sh4_clear_interrupt(sh4, SH4_INT_IRL_9);
    }
  }

  {
    if ((*hl->SB_ISTNRM & *hl->SB_IML4NRM) ||
        (*hl->SB_ISTERR & *hl->SB_IML4ERR) ||
        (*hl->SB_ISTEXT & *hl->SB_IML4EXT)) {
      sh4_raise_interrupt(sh4, SH4_INT_IRL_11);
    } else {
      sh4_clear_interrupt(sh4, SH4_INT_IRL_11);
    }
  }

  {
    if ((*hl->SB_ISTNRM & *hl->SB_IML2NRM) ||
        (*hl->SB_ISTERR & *hl->SB_IML2ERR) ||
        (*hl->SB_ISTEXT & *hl->SB_IML2EXT)) {
      sh4_raise_interrupt(sh4, SH4_INT_IRL_13);
    } else {
      sh4_clear_interrupt(sh4, SH4_INT_IRL_13);
    }
  }
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

void holly_reg_write(struct holly *hl, uint32_t addr, uint32_t data,
                     uint32_t mask) {
  uint32_t offset = addr >> 2;
  reg_write_cb write = holly_cb[offset].write;

  if (hl->log_regs) {
    LOG_INFO("holly_reg_write addr=0x%08x data=0x%x", addr, data & mask);
  }

  if (write) {
    write(hl->dc, data);
    return;
  }

  hl->reg[offset] = data;
}

uint32_t holly_reg_read(struct holly *hl, uint32_t addr, uint32_t mask) {
  uint32_t offset = addr >> 2;
  reg_read_cb read = holly_cb[offset].read;

  uint32_t data;
  if (read) {
    data = read(hl->dc);
  } else {
    data = hl->reg[offset];
  }

  if (hl->log_regs) {
    LOG_INFO("holly_reg_read addr=0x%08x data=0x%x", addr, data);
  }

  return data;
}

#ifdef HAVE_IMGUI
void holly_debug_menu(struct holly *hl) {
  if (igBeginMainMenuBar()) {
    if (igBeginMenu("HOLLY", 1)) {
      if (igMenuItem("log reg access", NULL, hl->log_regs, 1)) {
        hl->log_regs = !hl->log_regs;
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
      dc_create_device(dc, sizeof(struct holly), "holly", &holly_init, NULL);

/* init registers */
#define HOLLY_REG(addr, name, default, type) \
  hl->reg[name] = default;                   \
  hl->name = (type *)&hl->reg[name];
#include "guest/holly/holly_regs.inc"
#undef HOLLY_REG

  return hl;
}

REG_R32(holly_cb, SB_FFST) {
  /* most code i've seen that reads this register seems to block until the bit
     it's interested in is 0, signalling that the fifo is empty and able to be
     written to. being that the fifos aren't emulated, always returning zero
     seems sane */
  return 0;
}

REG_W32(holly_cb, SB_FFST) {}

REG_W32(holly_cb, SB_SFRES) {
  /* only reset if the magic value is written */
  if (value != 0x7611) {
    return;
  }

  LOG_FATAL("software reset through SB_SFRES unsupported");
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

  int suspend = value & 0x1;

  if (hl->SB_ADTSEL->susp && suspend) {
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

REG_R32(holly_cb, SB_ADSTAGD) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[0];
  return dma->dst;
}

REG_R32(holly_cb, SB_ADSTARD) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[0];
  return dma->src;
}

REG_R32(holly_cb, SB_ADLEND) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[0];
  return dma->len;
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

  int suspend = value & 0x1;

  if (hl->SB_E1TSEL->susp && suspend) {
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

REG_R32(holly_cb, SB_E1STAGD) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[1];
  return dma->dst;
}

REG_R32(holly_cb, SB_E1STARD) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[1];
  return dma->src;
}

REG_R32(holly_cb, SB_E1LEND) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[1];
  return dma->len;
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

  int suspend = value & 0x1;

  if (hl->SB_E2TSEL->susp && suspend) {
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

REG_R32(holly_cb, SB_E2STAGD) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[2];
  return dma->dst;
}

REG_R32(holly_cb, SB_E2STARD) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[2];
  return dma->src;
}

REG_R32(holly_cb, SB_E2LEND) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[2];
  return dma->len;
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

  int suspend = value & 0x1;

  if (hl->SB_DDTSEL->susp && suspend) {
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

REG_R32(holly_cb, SB_DDSTAGD) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[3];
  return dma->dst;
}

REG_R32(holly_cb, SB_DDSTARD) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[3];
  return dma->src;
}

REG_R32(holly_cb, SB_DDLEND) {
  struct holly *hl = dc->holly;
  struct holly_g2_dma *dma = &hl->dma[3];
  return dma->len;
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
