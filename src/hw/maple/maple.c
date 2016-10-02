#include "hw/maple/maple.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/sh4/sh4.h"

struct maple {
  struct device;
  struct maple_device *devices[4];
};

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

int maple_handle_command(struct maple *mp, int port, struct maple_frame *frame,
                         struct maple_frame *res) {
  struct maple_device *dev = mp->devices[port];

  if (!dev) {
    return 0;
  }

  return dev->frame(dev, frame, res);
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
