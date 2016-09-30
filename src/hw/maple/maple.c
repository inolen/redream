#include "hw/maple/maple.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/sh4/sh4.h"

struct maple {
  struct device;
  struct maple_device *devices[4];
};

static void maple_dma(struct maple *mp) {
  struct address_space *space = mp->sh4->memory_if->space;
  uint32_t start_addr = mp->holly->reg[SB_MDSTAR];
  union maple_transfer desc;
  struct maple_frame frame, res;

  do {
    desc.full = as_read64(space, start_addr);
    start_addr += 8;

    // read input
    frame.header.full = as_read32(space, start_addr);
    start_addr += 4;

    for (uint32_t i = 0; i < frame.header.num_words; i++) {
      frame.params[i] = as_read32(space, start_addr);
      start_addr += 4;
    }

    // handle frame and write response
    struct maple_device *dev = mp->devices[desc.port];

    if (dev && dev->frame(dev, &frame, &res)) {
      as_write32(space, desc.result_addr, res.header.full);
      desc.result_addr += 4;

      for (uint32_t i = 0; i < res.header.num_words; i++) {
        as_write32(space, desc.result_addr, res.params[i]);
        desc.result_addr += 4;
      }
    } else {
      as_write32(space, desc.result_addr, 0xffffffff);
    }
  } while (!desc.last);

  mp->holly->reg[SB_MDST] = 0;
  holly_raise_interrupt(mp->holly, HOLLY_INTC_MDEINT);
}

static bool maple_init(struct device *dev) {
  // struct maple *mp = (struct maple *)dev;
  return true;
}

static void maple_keydown(struct device *dev, enum keycode key, int16_t value) {
  struct maple *mp = (struct maple *)dev;
  struct maple_device *mp_dev = mp->devices[0];

  if (mp_dev) {
    mp_dev->input(mp_dev, key, value);
  }
}

void maple_vblank(struct maple *mp) {
  uint32_t enabled = mp->holly->reg[SB_MDEN];
  uint32_t vblank_initiate = mp->holly->reg[SB_MDTSEL];

  // The controller can be started up by two methods: by software, or by
  // hardware
  // in synchronization with the V-BLANK signal. These methods are selected
  // through the trigger selection register (SB_MDTSEL).
  if (enabled && vblank_initiate) {
    maple_dma(mp);
  }

  // TODO maple vblank interrupt?
}

struct maple *maple_create(struct dreamcast *dc) {
  struct maple *mp =
      dc_create_device(dc, sizeof(struct maple), "maple", &maple_init);
  mp->window_if = dc_create_window_interface(NULL, NULL, &maple_keydown);

  mp->devices[0] = controller_create();

  return mp;
}

void maple_destroy(struct maple *mp) {
  for (int i = 0; i < array_size(mp->devices); i++) {
    struct maple_device *dev = mp->devices[i];

    if (dev && dev->destroy) {
      dev->destroy(dev);
    }
  }

  dc_destroy_window_interface(mp->window_if);
  dc_destroy_device((struct device *)mp);
}

REG_W32(holly_cb, SB_MDST) {
  struct maple *mp = dc->maple;
  struct holly *hl = dc->holly;

  *hl->SB_MDST = value;

  if (*hl->SB_MDEN) {
    if (*hl->SB_MDST) {
      maple_dma(mp);
    }
  } else {
    *hl->SB_MDST = 0;
  }
}
