#include "guest/maple/maple.h"
#include "guest/dreamcast.h"
#include "guest/holly/holly.h"
#include "guest/sh4/sh4.h"

struct maple {
  struct device;
  struct maple_device *devs[MAPLE_NUM_PORTS][MAPLE_MAX_UNITS];
};

static void maple_unregister_dev(struct maple *mp, int port, int unit) {
  struct maple_device **dev = &mp->devs[port][unit];

  if (!*dev) {
    return;
  }

  if ((*dev)->destroy) {
    (*dev)->destroy(*dev);
  }

  *dev = NULL;
}

static void maple_register_dev(struct maple *mp, const char *device_type,
                               int port, int unit) {
  struct dreamcast *dc = mp->dc;

  CHECK(!mp->devs[port][unit],
        "maple_register_dev already registered for port=%d unit=%d", port,
        unit);
  struct maple_device **dev = &mp->devs[port][unit];

  if (!strcmp(device_type, "controller")) {
    *dev = controller_create(mp, port);
  } else if (!strcmp(device_type, "vmu")) {
    *dev = vmu_create(mp, port);
  } else {
    LOG_WARNING("Unsupported device type: %s", device_type);
  }
}

int maple_handle_frame(struct maple *mp, int port, union maple_frame *frame,
                       union maple_frame *res) {
  CHECK(port >= 0 && port < MAPLE_NUM_PORTS);

  /* note, not all maple devices have the same frame format. for example, the
     check-gd disc sends a command to some kind of debug device which has the
     frame data bswap'd. for this reason, it's not valid to inspect the frame
     data in order to send the frame directly to the correct sub-device */
  struct maple_device *dev = mp->devs[port][MAPLE_MAX_UNITS - 1];
  if (!dev) {
    return 0;
  }

  dev->frame(dev, frame, res);
  return 1;
}

void maple_handle_input(struct maple *mp, int port, int button,
                        uint16_t value) {
  CHECK(port >= 0 && port < MAPLE_NUM_PORTS);

  /* send to primary device */
  struct maple_device *dev = mp->devs[port][MAPLE_MAX_UNITS - 1];

  if (dev && dev->input) {
    dev->input(dev, button, value);
  }
}

struct maple_device *maple_get_device(struct maple *mp, int port, int unit) {
  return mp->devs[port][unit];
}

static int maple_init(struct device *dev) {
  return 1;
}

void maple_destroy(struct maple *mp) {
  for (int i = 0; i < MAPLE_NUM_PORTS; i++) {
    for (int j = 0; j < MAPLE_MAX_UNITS; j++) {
      maple_unregister_dev(mp, i, j);
    }
  }

  dc_destroy_device((struct device *)mp);
}

struct maple *maple_create(struct dreamcast *dc) {
  struct maple *mp =
      dc_create_device(dc, sizeof(struct maple), "maple", &maple_init, NULL);

  /* register a controller and vmu for all ports by default */
  for (int i = 0; i < MAPLE_NUM_PORTS; i++) {
    maple_register_dev(mp, "controller", i, 5);
    maple_register_dev(mp, "vmu", i, 0);
  }

  return mp;
}

/* on each maple port, there are up to 6 addressable units. there is one main
   unit (controller, keyboard, etc.) that can have up to 5 sub-units connected
   to it (vmu, microphone, etc.). each maple frame header contains an 8-bit
   address specifying the port and unit it's intended for that looks like:

   7-6  5          4           3           2           1           0
   port main unit  sub-unit 5  sub-unit 4  sub-unit 3  sub-unit 2  sub-unit 1 */
int maple_decode_addr(uint32_t addr, int *port, int *unit) {
  *port = addr >> 6;
  *unit = 0;

  for (int i = 0; i < MAPLE_MAX_UNITS; i++) {
    if (addr & (1 << i)) {
      *unit = i;
    }
  }

  return (*port >= 0 && *port < MAPLE_NUM_PORTS) &&
         (*unit >= 0 && *unit < MAPLE_MAX_UNITS);
}

uint8_t maple_encode_addr(int port, int unit) {
  CHECK(port >= 0 && port < MAPLE_NUM_PORTS);
  CHECK(unit >= 0 && unit < MAPLE_MAX_UNITS);
  return (port << 6) | (1 << unit);
}
