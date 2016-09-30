#include "hw/holly/holly.h"
#include "hw/dreamcast.h"
#include "hw/gdrom/gdrom.h"
#include "hw/maple/maple.h"
#include "hw/sh4/sh4.h"

struct reg_cb holly_cb[NUM_HOLLY_REGS];

static void holly_update_sh4_interrupts(struct holly *hl) {
  // trigger the respective level-encoded interrupt on the sh4 interrupt
  // controller
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

#define define_reg_read(name, type)                        \
  type holly_reg_##name(struct holly *hl, uint32_t addr) { \
    uint32_t offset = addr >> 2;                           \
    reg_read_cb read = holly_cb[offset].read;              \
    if (read) {                                            \
      return (type)read(hl->dc);                           \
    }                                                      \
    return (type)hl->reg[offset];                          \
  }

define_reg_read(r8, uint8_t);
define_reg_read(r16, uint16_t);
define_reg_read(r32, uint32_t);

#define define_reg_write(name, type)                                   \
  void holly_reg_##name(struct holly *hl, uint32_t addr, type value) { \
    uint32_t offset = addr >> 2;                                       \
    reg_write_cb write = holly_cb[offset].write;                       \
    if (write) {                                                       \
      write(hl->dc, value);                                            \
      return;                                                          \
    }                                                                  \
    hl->reg[offset] = (uint32_t)value;                                 \
  }

define_reg_write(w8, uint8_t);
define_reg_write(w16, uint16_t);
define_reg_write(w32, uint32_t);

static bool holly_init(struct device *dev) {
  struct holly *hl = (struct holly *)dev;
  return true;
}

void holly_raise_interrupt(struct holly *hl, holly_interrupt_t intr) {
  enum holly_interrupt_type type = HOLLY_INTERRUPT_TYPE(intr);
  uint32_t irq = HOLLY_INTERRUPT_IRQ(intr);

  if (intr == HOLLY_INTC_PCVOINT) {
    maple_vblank(hl->maple);
  }

  switch (type) {
    case HOLLY_INTC_NRM:
      *hl->SB_ISTNRM |= irq;
      break;

    case HOLLY_INTC_EXT:
      *hl->SB_ISTEXT |= irq;
      break;

    case HOLLY_INTC_ERR:
      *hl->SB_ISTERR |= irq;
      break;
  }

  holly_update_sh4_interrupts(hl);
}

void holly_clear_interrupt(struct holly *hl, holly_interrupt_t intr) {
  enum holly_interrupt_type type = HOLLY_INTERRUPT_TYPE(intr);
  uint32_t irq = HOLLY_INTERRUPT_IRQ(intr);

  switch (type) {
    case HOLLY_INTC_NRM:
      *hl->SB_ISTNRM &= ~irq;
      break;

    case HOLLY_INTC_EXT:
      *hl->SB_ISTEXT &= ~irq;
      break;

    case HOLLY_INTC_ERR:
      *hl->SB_ISTERR &= ~irq;
      break;
  }

  holly_update_sh4_interrupts(hl);
}

struct holly *holly_create(struct dreamcast *dc) {
  struct holly *hl =
      dc_create_device(dc, sizeof(struct holly), "holly", &holly_init);

// init registers
#define HOLLY_REG(addr, name, default, type) \
  hl->reg[name] = default;                   \
  hl->name = (type *)&hl->reg[name];
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG

  return hl;
}

void holly_destroy(struct holly *hl) {
  dc_destroy_device((struct device *)hl);
}

REG_R32(holly_cb, SB_ISTNRM) {
  struct holly *hl = dc->holly;
  // Note that the two highest bits indicate the OR'ed result of all of the
  // bits in SB_ISTEXT and SB_ISTERR, respectively, and writes to these two
  // bits are ignored.
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
  // writing a 1 clears the interrupt
  *hl->SB_ISTNRM &= ~value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_ISTEXT) {
  struct holly *hl = dc->holly;
  *hl->SB_ISTEXT &= ~value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_ISTERR) {
  struct holly *hl = dc->holly;
  *hl->SB_ISTERR &= ~value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_IML2NRM) {
  struct holly *hl = dc->holly;
  *hl->SB_IML2NRM = value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_IML2EXT) {
  struct holly *hl = dc->holly;
  *hl->SB_IML2EXT = value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_IML2ERR) {
  struct holly *hl = dc->holly;
  *hl->SB_IML2ERR = value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_IML4NRM) {
  struct holly *hl = dc->holly;
  *hl->SB_IML4NRM = value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_IML4EXT) {
  struct holly *hl = dc->holly;
  *hl->SB_IML4EXT = value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_IML4ERR) {
  struct holly *hl = dc->holly;
  *hl->SB_IML4ERR = value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_IML6NRM) {
  struct holly *hl = dc->holly;
  *hl->SB_IML6NRM = value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_IML6EXT) {
  struct holly *hl = dc->holly;
  *hl->SB_IML6EXT = value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_IML6ERR) {
  struct holly *hl = dc->holly;
  *hl->SB_IML6ERR = value;
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_cb, SB_C2DST) {
  struct holly *hl = dc->holly;

  *hl->SB_C2DST = value;

  if (!*hl->SB_C2DST) {
    return;
  }

  // FIXME what are SB_LMMODE0 / SB_LMMODE1
  struct sh4_dtr dtr = {0};
  dtr.channel = 2;
  dtr.rw = false;
  dtr.addr = *hl->SB_C2DSTAT;
  sh4_ddt(hl->sh4, &dtr);

  *hl->SB_C2DLEN = 0;
  *hl->SB_C2DST = 0;
  holly_raise_interrupt(hl, HOLLY_INTC_DTDE2INT);
}

REG_W32(holly_cb, SB_SDST) {
  struct holly *hl = dc->holly;
  *hl->SB_SDST = value;
  if (!*hl->SB_SDST) {
    return;
  }
  LOG_FATAL("Sort DMA not supported");
}

REG_W32(holly_cb, SB_GDST) {
  struct gdrom *gd = dc->gdrom;
  struct holly *hl = dc->holly;

  // if a "0" is written to this register, it is ignored
  *hl->SB_GDST |= value;

  if (!*hl->SB_GDST) {
    return;
  }

  CHECK_EQ(*hl->SB_GDEN, 1);   // dma enabled
  CHECK_EQ(*hl->SB_GDDIR, 1);  // gd-rom -> system memory

  int transfer_size = *hl->SB_GDLEN;
  uint32_t start = *hl->SB_GDSTAR;

  int remaining = transfer_size;
  uint32_t addr = start;

  gdrom_dma_begin(gd);

  while (remaining) {
    // read a single sector at a time from the gdrom
    uint8_t sector_data[SECTOR_SIZE];
    int n = gdrom_dma_read(gd, sector_data, sizeof(sector_data));

    struct sh4_dtr dtr = {0};
    dtr.channel = 0;
    dtr.rw = true;
    dtr.data = sector_data;
    dtr.addr = addr;
    dtr.size = n;
    sh4_ddt(dc->sh4, &dtr);

    remaining -= n;
    addr += n;
  }

  gdrom_dma_end(gd);

  *hl->SB_GDSTARD = start + transfer_size;
  *hl->SB_GDLEND = transfer_size;
  *hl->SB_GDST = 0;
  holly_raise_interrupt(hl, HOLLY_INTC_G1DEINT);
}

REG_W32(holly_cb, SB_ADEN) {
  struct holly *hl = dc->holly;
  *hl->SB_ADEN = value;
  if (!*hl->SB_ADEN) {
    return;
  }
  LOG_WARNING("Ignored aica DMA request");
}

REG_W32(holly_cb, SB_ADST) {
  struct holly *hl = dc->holly;
  *hl->SB_ADST = value;
  if (!*hl->SB_ADST) {
    return;
  }
  LOG_WARNING("Ignored aica DMA request");
}

REG_W32(holly_cb, SB_E1EN) {
  struct holly *hl = dc->holly;
  *hl->SB_E1EN = value;
  if (!*hl->SB_E1EN) {
    return;
  }
  LOG_WARNING("Ignored ext1 DMA request");
}

REG_W32(holly_cb, SB_E1ST) {
  struct holly *hl = dc->holly;
  *hl->SB_E1ST = value;
  if (!*hl->SB_E1ST) {
    return;
  }
  LOG_WARNING("Ignored ext1 DMA request");
}

REG_W32(holly_cb, SB_E2EN) {
  struct holly *hl = dc->holly;
  *hl->SB_E2EN = value;
  if (!*hl->SB_E2EN) {
    return;
  }
  LOG_WARNING("Ignored ext2 DMA request");
}

REG_W32(holly_cb, SB_E2ST) {
  struct holly *hl = dc->holly;
  *hl->SB_E2ST = value;
  if (!*hl->SB_E2ST) {
    return;
  }
  LOG_WARNING("Ignored ext2 DMA request");
}

REG_W32(holly_cb, SB_DDEN) {
  struct holly *hl = dc->holly;
  *hl->SB_DDEN = value;
  if (!*hl->SB_DDEN) {
    return;
  }
  LOG_WARNING("Ignored dev DMA request");
}

REG_W32(holly_cb, SB_DDST) {
  struct holly *hl = dc->holly;
  *hl->SB_DDST = value;
  if (!*hl->SB_DDST) {
    return;
  }
  LOG_WARNING("Ignored dev DMA request");
}

REG_W32(holly_cb, SB_PDEN) {
  struct holly *hl = dc->holly;
  *hl->SB_PDEN = value;
  if (!*hl->SB_PDEN) {
    return;
  }
  LOG_WARNING("Ignored pvr DMA request");
}

REG_W32(holly_cb, SB_PDST) {
  struct holly *hl = dc->holly;
  *hl->SB_PDST = value;
  if (!*hl->SB_PDST) {
    return;
  }
  LOG_WARNING("Ignored pvr DMA request");
}

// clang-format off
AM_BEGIN(struct holly, holly_reg_map);
  AM_RANGE(0x00000000, 0x00001fff) AM_HANDLE("holly reg",
                                             (r8_cb)&holly_reg_r8,
                                             (r16_cb)&holly_reg_r16,
                                             (r32_cb)&holly_reg_r32,
                                             NULL,
                                             (w8_cb)&holly_reg_w8,
                                             (w16_cb)&holly_reg_w16,
                                             (w32_cb)&holly_reg_w32,
                                             NULL)
AM_END();
// clang-format on
