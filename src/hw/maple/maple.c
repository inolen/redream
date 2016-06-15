#include "hw/holly/holly.h"
#include "hw/maple/maple.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"

typedef struct maple_s {
  device_t base;

  holly_t *holly;
  address_space_t *space;
  maple_device_t *devices[4];
} maple_t;

static void maple_dma(maple_t *mp) {
  uint32_t start_addr = mp->holly->reg[SB_MDSTAR];
  maple_transfer_t desc;
  maple_frame_t frame, res;

  do {
    desc.full = as_read64(mp->space, start_addr);
    start_addr += 8;

    // read input
    frame.header.full = as_read32(mp->space, start_addr);
    start_addr += 4;

    for (uint32_t i = 0; i < frame.header.num_words; i++) {
      frame.params[i] = as_read32(mp->space, start_addr);
      start_addr += 4;
    }

    // handle frame and write response
    maple_device_t *dev = mp->devices[desc.port];

    if (dev && dev->frame(dev, &frame, &res)) {
      as_write32(mp->space, desc.result_addr, res.header.full);
      desc.result_addr += 4;

      for (uint32_t i = 0; i < res.header.num_words; i++) {
        as_write32(mp->space, desc.result_addr, res.params[i]);
        desc.result_addr += 4;
      }
    } else {
      as_write32(mp->space, desc.result_addr, 0xffffffff);
    }
  } while (!desc.last);

  mp->holly->reg[SB_MDST] = 0;
  holly_raise_interrupt(mp->holly, HOLLY_INTC_MDEINT);
}

REG_W32(maple_t *mp, SB_MDST) {
  uint32_t enabled = mp->holly->reg[SB_MDEN];
  if (enabled) {
    if (*new_value) {
      maple_dma(mp);
    }
  } else {
    *new_value = 0;
  }
}

static bool maple_init(maple_t *mp) {
  dreamcast_t *dc = mp->base.dc;

  mp->holly = dc->holly;
  mp->space = dc->sh4->base.memory->space;

#define MAPLE_REG_R32(name)       \
  mp->holly->reg_data[name] = mp; \
  mp->holly->reg_read[name] = (reg_read_cb)&name##_r;
#define MAPLE_REG_W32(name)       \
  mp->holly->reg_data[name] = mp; \
  mp->holly->reg_write[name] = (reg_write_cb)&name##_w;
  MAPLE_REG_W32(SB_MDST);
#undef MAPLE_REG_R32
#undef MAPLE_REG_W32
  return true;
}

static void maple_keydown(maple_t *mp, keycode_t key, int16_t value) {
  maple_device_t *dev = mp->devices[0];

  if (!dev) {
    return;
  }

  dev->input(dev, key, value);
}

void maple_vblank(maple_t *mp) {
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

maple_t *maple_create(dreamcast_t *dc) {
  maple_t *mp = dc_create_device(dc, sizeof(maple_t), "maple",
                                 (device_init_cb)&maple_init);
  mp->base.window =
      window_interface_create(NULL, (device_keydown_cb)&maple_keydown);

  mp->devices[0] = controller_create();

  return mp;
}

void maple_destroy(maple_t *mp) {
  for (int i = 0; i < array_size(mp->devices); i++) {
    maple_device_t *dev = mp->devices[i];

    if (dev && dev->destroy) {
      dev->destroy(dev);
    }
  }

  window_interface_destroy(mp->base.window);
  dc_destroy_device(&mp->base);
}
