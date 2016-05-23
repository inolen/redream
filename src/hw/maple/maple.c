#include "hw/holly/holly.h"
#include "hw/maple/maple.h"
#include "hw/maple/controller.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"

static const int MAX_PORTS = 4;

typedef struct maple_s {
  device_t base;

  holly_t *holly;
  address_space_t *space;

  maple_device_t *devices[MAX_PORTS];
} maple_t;

static bool maple_init(maple_t *mp);
static void maple_dma(maple_t *mp);
static void maple_keydown(maple_t *mp, keycode_t key, int16_t value);
DECLARE_REG_W32(maple_t *mp, SB_MDST);

struct maple_s *maple_create(struct dreamcast_s *dc) {
  maple_t *mp = dc_create_device(dc, sizeof(maple_t), "maple",
                                 (device_init_cb)&maple_init);
  mp->base.window =
      window_interface_create(NULL, (device_keydown_cb)&maple_keydown);

  mp->devices[0] = maple_create_controller();

  return mp;
}

void maple_destroy(maple_t *mp) {
  for (int i = 0; i < MAX_PORTS; i++) {
    maple_device_t *dev = mp->devices[i];

    if (dev && dev->destroy) {
      dev->destroy(dev);
    }
  }

  window_interface_destroy(mp->base.window);
  dc_destroy_device(&mp->base);
}

bool maple_init(maple_t *mp) {
  mp->holly = mp->base.dc->holly;
  mp->space = mp->base.dc->sh4->base.memory->space;

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

void maple_dma(maple_t *mp) {
  uint32_t start_addr = mp->holly->reg[SB_MDSTAR];
  maple_transfer_t desc;
  maple_frame_t frame, res;

  do {
    desc.full = address_space_r64(mp->space, start_addr);
    start_addr += 8;

    // read input
    frame.header.full = address_space_r32(mp->space, start_addr);
    start_addr += 4;

    for (uint32_t i = 0; i < frame.header.num_words; i++) {
      frame.params[i] = address_space_r32(mp->space, start_addr);
      start_addr += 4;
    }

    // handle frame and write response
    maple_device_t *dev = mp->devices[desc.port];

    if (dev && dev->frame(dev, &frame, &res)) {
      address_space_w32(mp->space, desc.result_addr, res.header.full);
      desc.result_addr += 4;

      for (uint32_t i = 0; i < res.header.num_words; i++) {
        address_space_w32(mp->space, desc.result_addr, res.params[i]);
        desc.result_addr += 4;
      }
    } else {
      address_space_w32(mp->space, desc.result_addr, 0xffffffff);
    }
  } while (!desc.last);

  mp->holly->reg[SB_MDST] = 0;
  holly_raise_interrupt(mp->holly, HOLLY_INTC_MDEINT);
}

void maple_keydown(maple_t *mp, keycode_t key, int16_t value) {
  maple_device_t *dev = mp->devices[0];

  if (!dev) {
    return;
  }

  dev->input(dev, key, value);
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
