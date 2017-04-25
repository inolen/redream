#include "hw/maple/maple.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/sh4/sh4.h"

#define MAPLE_NUM_PORTS 4
#define MAPLE_MAX_UNITS 6

struct maple {
  struct device;
  struct maple_device *devices[MAPLE_NUM_PORTS][MAPLE_MAX_UNITS];
};

static void maple_unregister_device(struct maple *mp, int port, int unit) {
  struct maple_device **dev = &mp->devices[port][unit];

  if (!*dev) {
    return;
  }

  if ((*dev)->destroy) {
    (*dev)->destroy(*dev);
  }

  *dev = NULL;
}

static void maple_register_device(struct maple *mp, const char *device_type,
                                  int port, int unit) {
  CHECK(!mp->devices[port][unit],
        "Device already registered for port %d, unit %d", port, unit);
  struct maple_device **dev = &mp->devices[port][unit];

  if (!strcmp(device_type, "controller")) {
    *dev = controller_create(port, unit);
  } else if (!strcmp(device_type, "vmu")) {
    *dev = vmu_create(port, unit);
  } else {
    LOG_WARNING("Unsupported device type: %s", device_type);
  }
}

void maple_joy_add(struct device *data, int joystick_index) {
  struct maple *mp = (struct maple *)data;

  /* ignore out of range events or events for the default controller which is
     always connected */
  if (joystick_index <= 0 || joystick_index >= MAPLE_NUM_PORTS) {
    return;
  }

  /* attach joystick to the corresponding maple port */
  maple_register_device(mp, "controller", joystick_index, 0);
}

void maple_joy_remove(struct device *data, int joystick_index) {
  struct maple *mp = (struct maple *)data;

  /* ignore out of range events or events for the default controller which is
     always connected */
  if (joystick_index <= 0 || joystick_index >= MAPLE_NUM_PORTS) {
    return;
  }

  /* remove all units from the corresponding maple port */
  for (int i = 0; i < MAPLE_MAX_UNITS; i++) {
    maple_unregister_device(mp, joystick_index, i);
  }
}

static void maple_keydown(struct device *d, int device_index, enum keycode key,
                          int16_t value) {
  if (device_index >= MAPLE_NUM_PORTS) {
    return;
  }

  struct maple *mp = (struct maple *)d;

  for (int i = 0; i < MAPLE_MAX_UNITS; i++) {
    struct maple_device *dev = mp->devices[device_index][i];

    if (dev && dev->input) {
      dev->input(dev, key, value);
    }
  }
}

/* on each maple port, there are up to 6 addressable units. there is one main
   unit (controller, keyboard, etc.) that can have up to 5 sub-units connected
   to it (vmu, microphone, etc.). each maple frame header contains an 8-bit
   address specifying the port and unit it's intended for that looks like:

   7-6  5          4           3           2           1           0
   port main unit  sub-unit 5  sub-unit 4  sub-unit 3  sub-unit 2  sub-unit 1 */
static uint8_t maple_encode_addr(int port, int unit) {
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

static void maple_decode_addr(uint32_t addr, int *port, int *unit) {
  *port = addr >> 6;
  CHECK_LT(*port, MAPLE_NUM_PORTS);

  /* prioritize the main unit, else return the first matching sub-unit */
  *unit = -1;
  if (addr & (1 << (MAPLE_MAX_UNITS - 1))) {
    *unit = 0;
  } else {
    for (int i = 1; i < MAPLE_MAX_UNITS; i++) {
      if (addr & (1 << (i - 1))) {
        *unit = i;
        break;
      }
    }
  }
  CHECK(*unit >= 0 && *unit < MAPLE_MAX_UNITS);
}

int maple_handle_command(struct maple *mp, struct maple_frame *frame,
                         struct maple_frame *res) {
  int p, u;
  maple_decode_addr(frame->header.recv_addr, &p, &u);

  struct maple_device *dev = mp->devices[p][u];

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
      struct maple_device *sub = mp->devices[p][i];
      if (sub) {
        res->header.send_addr |= (1 << (i - 1));
      }
    }
  }

  return 1;
}

static int maple_init(struct device *dev) {
  return 1;
}

void maple_destroy(struct maple *mp) {
  for (int i = 0; i < MAPLE_NUM_PORTS; i++) {
    for (int j = 0; j < MAPLE_MAX_UNITS; j++) {
      maple_unregister_device(mp, i, j);
    }
  }

  dc_destroy_window_interface(mp->window_if);
  dc_destroy_device((struct device *)mp);
}

struct maple *maple_create(struct dreamcast *dc) {
  struct maple *mp =
      dc_create_device(dc, sizeof(struct maple), "maple", &maple_init, NULL);
  mp->window_if = dc_create_window_interface(&maple_keydown, &maple_joy_add,
                                             &maple_joy_remove);

  /* add one controller and vmu by default */
  maple_register_device(mp, "controller", 0, 0);
  maple_register_device(mp, "vmu", 0, 1);

  return mp;
}
