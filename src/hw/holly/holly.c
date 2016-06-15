#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/maple/maple.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"

static void holly_update_sh4_interrupts(holly_t *hl) {
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

#define define_reg_read(name, type)                   \
  type holly_reg_##name(holly_t *hl, uint32_t addr) { \
    uint32_t offset = addr >> 2;                      \
    reg_read_cb read = hl->reg_read[offset];          \
                                                      \
    if (read) {                                       \
      void *data = hl->reg_data[offset];              \
      return (type)read(data);                        \
    }                                                 \
                                                      \
    return (type)hl->reg[offset];                     \
  }

define_reg_read(r8, uint8_t);
define_reg_read(r16, uint16_t);
define_reg_read(r32, uint32_t);

#define define_reg_write(name, type)                              \
  void holly_reg_##name(holly_t *hl, uint32_t addr, type value) { \
    uint32_t offset = addr >> 2;                                  \
    reg_write_cb write = hl->reg_write[offset];                   \
                                                                  \
    uint32_t old_value = hl->reg[offset];                         \
    hl->reg[offset] = (uint32_t)value;                            \
                                                                  \
    if (write) {                                                  \
      void *data = hl->reg_data[offset];                          \
      write(data, old_value, &hl->reg[offset]);                   \
    }                                                             \
  }

define_reg_write(w8, uint8_t);
define_reg_write(w16, uint16_t);
define_reg_write(w32, uint32_t);

REG_R32(holly_t *hl, SB_ISTNRM) {
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

REG_W32(holly_t *hl, SB_ISTNRM) {
  // writing a 1 clears the interrupt
  *new_value = old_value & ~(*new_value);
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_ISTEXT) {
  *new_value = old_value & ~(*new_value);
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_ISTERR) {
  *new_value = old_value & ~(*new_value);
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_IML2NRM) {
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_IML2EXT) {
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_IML2ERR) {
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_IML4NRM) {
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_IML4EXT) {
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_IML4ERR) {
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_IML6NRM) {
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_IML6EXT) {
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_IML6ERR) {
  holly_update_sh4_interrupts(hl);
}

REG_W32(holly_t *hl, SB_C2DST) {
  if (!*new_value) {
    return;
  }

  // FIXME what are SB_LMMODE0 / SB_LMMODE1
  sh4_dtr_t dtr = {};
  dtr.channel = 2;
  dtr.rw = false;
  dtr.addr = *hl->SB_C2DSTAT;
  sh4_ddt(hl->sh4, &dtr);

  *hl->SB_C2DLEN = 0;
  *hl->SB_C2DST = 0;
  holly_raise_interrupt(hl, HOLLY_INTC_DTDE2INT);
}

REG_W32(holly_t *hl, SB_SDST) {
  if (!*new_value) {
    return;
  }

  LOG_FATAL("Sort DMA not supported");
}

REG_W32(holly_t *hl, SB_GDST) {
  // if a "0" is written to this register, it is ignored
  *new_value |= old_value;

  if (!*new_value) {
    return;
  }

  CHECK_EQ(*hl->SB_GDEN, 1);   // dma enabled
  CHECK_EQ(*hl->SB_GDDIR, 1);  // gd-rom -> system memory

  int transfer_size = *hl->SB_GDLEN;
  uint32_t start = *hl->SB_GDSTAR;

  int remaining = transfer_size;
  uint32_t addr = start;

  gdrom_dma_begin(hl->gdrom);

  while (remaining) {
    // read a single sector at a time from the gdrom
    uint8_t sector_data[SECTOR_SIZE];
    int n = gdrom_dma_read(hl->gdrom, sector_data, sizeof(sector_data));

    sh4_dtr_t dtr = {};
    dtr.channel = 0;
    dtr.rw = true;
    dtr.data = sector_data;
    dtr.addr = addr;
    dtr.size = n;
    sh4_ddt(hl->sh4, &dtr);

    remaining -= n;
    addr += n;
  }

  gdrom_dma_end(hl->gdrom);

  *hl->SB_GDSTARD = start + transfer_size;
  *hl->SB_GDLEND = transfer_size;
  *hl->SB_GDST = 0;
  holly_raise_interrupt(hl, HOLLY_INTC_G1DEINT);
}

REG_W32(holly_t *hl, SB_ADEN) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored aica DMA request");
}

REG_W32(holly_t *hl, SB_ADST) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored aica DMA request");
}

REG_W32(holly_t *hl, SB_E1EN) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored ext1 DMA request");
}

REG_W32(holly_t *hl, SB_E1ST) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored ext1 DMA request");
}

REG_W32(holly_t *hl, SB_E2EN) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored ext2 DMA request");
}

REG_W32(holly_t *hl, SB_E2ST) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored ext2 DMA request");
}

REG_W32(holly_t *hl, SB_DDEN) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored dev DMA request");
}

REG_W32(holly_t *hl, SB_DDST) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored dev DMA request");
}

REG_W32(holly_t *hl, SB_PDEN) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored pvr DMA request");
}

REG_W32(holly_t *hl, SB_PDST) {
  if (!*new_value) {
    return;
  }

  LOG_WARNING("Ignored pvr DMA request");
}

static bool holly_init(holly_t *hl) {
  dreamcast_t *dc = hl->base.dc;

  hl->gdrom = dc->gdrom;
  hl->maple = dc->maple;
  hl->sh4 = dc->sh4;

#define HOLLY_REG_R32(name) \
  hl->reg_data[name] = hl;  \
  hl->reg_read[name] = (reg_read_cb)&name##_r;
#define HOLLY_REG_W32(name) \
  hl->reg_data[name] = hl;  \
  hl->reg_write[name] = (reg_write_cb)&name##_w;
  HOLLY_REG_R32(SB_ISTNRM);
  HOLLY_REG_W32(SB_ISTNRM);
  HOLLY_REG_W32(SB_ISTEXT);
  HOLLY_REG_W32(SB_ISTERR);
  HOLLY_REG_W32(SB_IML2NRM);
  HOLLY_REG_W32(SB_IML2EXT);
  HOLLY_REG_W32(SB_IML2ERR);
  HOLLY_REG_W32(SB_IML4NRM);
  HOLLY_REG_W32(SB_IML4EXT);
  HOLLY_REG_W32(SB_IML4ERR);
  HOLLY_REG_W32(SB_IML6NRM);
  HOLLY_REG_W32(SB_IML6EXT);
  HOLLY_REG_W32(SB_IML6ERR);
  HOLLY_REG_W32(SB_C2DST);
  HOLLY_REG_W32(SB_SDST);
  HOLLY_REG_W32(SB_GDST);
  HOLLY_REG_W32(SB_ADEN);
  HOLLY_REG_W32(SB_ADST);
  HOLLY_REG_W32(SB_E1EN);
  HOLLY_REG_W32(SB_E1ST);
  HOLLY_REG_W32(SB_E2EN);
  HOLLY_REG_W32(SB_E2ST);
  HOLLY_REG_W32(SB_DDEN);
  HOLLY_REG_W32(SB_DDST);
  HOLLY_REG_W32(SB_PDEN);
  HOLLY_REG_W32(SB_PDST);
#undef HOLLY_REG_R32
#undef HOLLY_REG_W32

#define HOLLY_REG(addr, name, default, type) \
  hl->reg[name] = default;                   \
  hl->name = (type *)&hl->reg[name];
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG

  return true;
}

void holly_raise_interrupt(holly_t *hl, holly_interrupt_t intr) {
  holly_interrupt_type_t type =
      (holly_interrupt_type_t)(intr & HOLLY_INTC_MASK);
  uint32_t irq = (uint32_t)(intr & ~HOLLY_INTC_MASK);

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

void holly_clear_interrupt(holly_t *hl, holly_interrupt_t intr) {
  holly_interrupt_type_t type =
      (holly_interrupt_type_t)(intr & HOLLY_INTC_MASK);
  uint32_t irq = (uint32_t)(intr & ~HOLLY_INTC_MASK);

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

holly_t *holly_create(dreamcast_t *dc) {
  holly_t *hl = dc_create_device(dc, sizeof(holly_t), "holly",
                                 (device_init_cb)&holly_init);

  return hl;
}

void holly_destroy(holly_t *hl) {
  dc_destroy_device(&hl->base);
}

// clang-format off
AM_BEGIN(holly_t, holly_reg_map);
  AM_RANGE(0x00000000, 0x00001fff) AM_HANDLE((r8_cb)&holly_reg_r8,
                                             (r16_cb)&holly_reg_r16,
                                             (r32_cb)&holly_reg_r32,
                                             NULL,
                                             (w8_cb)&holly_reg_w8,
                                             (w16_cb)&holly_reg_w16,
                                             (w32_cb)&holly_reg_w32,
                                             NULL)
AM_END();
// clang-format on
