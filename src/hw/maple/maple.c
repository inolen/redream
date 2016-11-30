#include "hw/maple/maple.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/sh4/sh4.h"

#define MAPLE_NUM_PORTS 4
#define MAPLE_MAX_UNITS 6

struct maple_port {
  struct maple_device *units[MAPLE_MAX_UNITS];
};

struct maple {
  struct device;
  struct maple_port ports[MAPLE_NUM_PORTS];
};

static bool maple_init(struct device *dev) {
  return true;
}

static void maple_keydown(struct device *d, enum keycode key, int16_t value) {
  struct maple *mp = (struct maple *)d;

  for (int i = 0; i < MAPLE_NUM_PORTS; i++) {
    struct maple_port *port = &mp->ports[i];

    for (int j = 0; j < MAPLE_MAX_UNITS; j++) {
      struct maple_device *dev = port->units[j];

      if (dev && dev->input) {
        dev->input(dev, key, value);
      }
    }
  }
}

/* on each maple port, there are up to 6 addressable units. there is one main
   unit (controller, keyboard, etc.) that can have up to 5 sub-units connected
   to it (vmu, microphone, etc.). each maple frame header contains an 8-bit
   address specifying the port and unit it's intended for that looks like:

   7-6  5          4           3           2           1           0
   port main unit  sub-unit 5  sub-unit 4  sub-unit 3  sub-unit 2  sub-unit 1 */
uint8_t maple_encode_addr(int port, int unit) {
  CHECK_LT(port, MAPLE_NUM_PORTS);
  CHECK_LT(unit, MAPLE_MAX_UNITS);

  uint8_t addr = port << 6;
  if (unit) {
    addr |= 1 << (unit - 1);
  } else {
    addr |= 1 << (MAPLE_MAX_UNITS - 1);
  }

  return addr;
}

void maple_decode_addr(uint32_t addr, int *port, int *unit) {
  *port = addr >> 6;
  *unit = -1;

  if (addr & (1 << (MAPLE_MAX_UNITS - 1))) {
    *unit = 0;
  } else {
    /* return the first matching sub-unit */
    for (int i = 1; i < MAPLE_MAX_UNITS; i++) {
      if (addr & (1 << (i - 1))) {
        *unit = i;
        break;
      }
    }
  }

  CHECK_NE(*unit, -1);
}

int maple_handle_command(struct maple *mp, struct maple_frame *frame,
                         struct maple_frame *res) {
  int p, u;
  maple_decode_addr(frame->header.recv_addr, &p, &u);

  struct maple_port *port = &mp->ports[p];
  struct maple_device *dev = port->units[u];

  if (!dev) {
    return 0;
  }

  if (!dev->frame(dev, frame, res)) {
    LOG_INFO("Unhandled maple cmd %d for port %d, unit %d",
             frame->header.command, p, u);
    return 0;
  }

  /* when a main peripheral identifies itself in the response to a command, it
     sets the sub-peripheral bit for each sub-peripheral that is connected in
     in addition to bit 5 */
  if (u == 0) {
    for (int i = 1; i < MAPLE_MAX_UNITS; i++) {
      struct maple_device *sub = port->units[i];
      if (sub) {
        res->header.send_addr |= (1 << (i - 1));
      }
    }
  }

  return 1;
}

void maple_destroy(struct maple *mp) {
  for (int i = 0; i < MAPLE_NUM_PORTS; i++) {
    struct maple_port *port = &mp->ports[i];

    for (int j = 0; j < MAPLE_MAX_UNITS; j++) {
      struct maple_device *dev = port->units[j];

      if (dev && dev->destroy) {
        dev->destroy(dev);
      }
    }
  }

  dc_destroy_window_interface(mp->window_if);
  dc_destroy_device((struct device *)mp);
}

struct maple *maple_create(struct dreamcast *dc) {
  struct maple *mp =
      dc_create_device(dc, sizeof(struct maple), "maple", &maple_init);
  mp->window_if = dc_create_window_interface(NULL, &maple_keydown);

  /* add one controller and vmu by default */
  mp->ports[0].units[0] = controller_create();
  mp->ports[0].units[1] = vmu_create();

  return mp;
}
