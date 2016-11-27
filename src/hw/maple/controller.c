#include <ini.h>
#include "core/log.h"
#include "core/option.h"
#include "core/string.h"
#include "hw/maple/maple.h"

DEFINE_OPTION_STRING(profile, "profiles/ps4.ini", "Controller profile");

enum {
  CONT_C = 0x1,
  CONT_B = 0x2,
  CONT_A = 0x4,
  CONT_START = 0x8,
  CONT_DPAD_UP = 0x10,
  CONT_DPAD_DOWN = 0x20,
  CONT_DPAD_LEFT = 0x40,
  CONT_DPAD_RIGHT = 0x80,
  CONT_Z = 0x100,
  CONT_Y = 0x200,
  CONT_X = 0x400,
  CONT_D = 0x800,
  CONT_DPAD2_UP = 0x1000,
  CONT_DPAD2_DOWN = 0x2000,
  CONT_DPAD2_LEFT = 0x4000,
  CONT_DPAD2_RIGHT = 0x8000,
  /* only used by internal button map */
  CONT_JOYX = 0x10000,
  CONT_JOYY = 0x20000,
  CONT_LTRIG = 0x40000,
  CONT_RTRIG = 0x80000
};

struct condition {
  uint32_t function;
  uint16_t buttons;
  uint8_t rtrig;
  uint8_t ltrig;
  uint8_t joyx;
  uint8_t joyy;
  uint8_t joyx2;
  uint8_t joyy2;
};

struct controller {
  struct maple_device;
  struct condition cnd;
  int map[K_NUM_KEYS];
};

/*
 * constant device info structure sent as response to CMD_REQDEVINFO to
 * identify the controller
 */
static struct maple_device_info controller_devinfo = {
    FN_CONTROLLER,
    {0xfe060f00, 0x0, 0x0},
    0xff,
    0,
    "Dreamcast Controller",
    "Produced By or Under License From SEGA ENTERPRISES,LTD.",
    0x01ae,
    0x01f4};

static int controller_ini_handler(void *user, const char *section,
                                  const char *name, const char *value) {
  struct controller *ctrl = user;

  int button = 0;
  if (!strcmp(name, "joyx")) {
    button = CONT_JOYX;
  } else if (!strcmp(name, "joyy")) {
    button = CONT_JOYY;
  } else if (!strcmp(name, "ltrig")) {
    button = CONT_LTRIG;
  } else if (!strcmp(name, "rtrig")) {
    button = CONT_RTRIG;
  } else if (!strcmp(name, "start")) {
    button = CONT_START;
  } else if (!strcmp(name, "a")) {
    button = CONT_A;
  } else if (!strcmp(name, "b")) {
    button = CONT_B;
  } else if (!strcmp(name, "x")) {
    button = CONT_X;
  } else if (!strcmp(name, "y")) {
    button = CONT_Y;
  } else if (!strcmp(name, "dpad_up")) {
    button = CONT_DPAD_UP;
  } else if (!strcmp(name, "dpad_down")) {
    button = CONT_DPAD_DOWN;
  } else if (!strcmp(name, "dpad_left")) {
    button = CONT_DPAD_LEFT;
  } else if (!strcmp(name, "dpad_right")) {
    button = CONT_DPAD_RIGHT;
  } else {
    LOG_WARNING("Unknown button %s", name);
    return 0;
  }

  enum keycode key = get_key_by_name(value);
  if (key == K_UNKNOWN) {
    LOG_WARNING("Unknown key %s", value);
    return 0;
  }

  ctrl->map[key] = button;

  return 1;
}

static void controller_load_profile(struct controller *ctrl, const char *path) {
  if (!*path) {
    return;
  }

  LOG_INFO("Loading controller profile %s", path);

  if (ini_parse(path, controller_ini_handler, ctrl) < 0) {
    LOG_WARNING("Failed to parse %s", path);
    return;
  }
}

static void controller_destroy(struct maple_device *dev) {
  struct controller *ctrl = (struct controller *)dev;

  free(ctrl);
}

static bool controller_input(struct maple_device *dev, enum keycode key,
                             int16_t value) {
  struct controller *ctrl = (struct controller *)dev;

  /* map incoming key to dreamcast button */
  int button = ctrl->map[key];

  /* scale incoming int16_t -> uint8_t */
  uint8_t scaled = ((int32_t)value - INT16_MIN) >> 8;

  if (!button) {
    return false;
  }

  if (button <= CONT_DPAD2_RIGHT) {
    if (value) {
      ctrl->cnd.buttons &= ~button;
    } else {
      ctrl->cnd.buttons |= button;
    }
  } else if (button == CONT_JOYX) {
    ctrl->cnd.joyx = scaled;
  } else if (button == CONT_JOYY) {
    ctrl->cnd.joyy = scaled;
  } else if (button == CONT_LTRIG) {
    ctrl->cnd.ltrig = scaled;
  } else if (button == CONT_RTRIG) {
    ctrl->cnd.rtrig = scaled;
  }

  return true;
}

static bool controller_frame(struct maple_device *dev,
                             const struct maple_frame *frame,
                             struct maple_frame *res) {
  struct controller *ctrl = (struct controller *)dev;

  switch (frame->header.command) {
    case CMD_REQDEVINFO:
      res->header.command = CMD_RESDEVINFO;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = sizeof(controller_devinfo) >> 2;
      memcpy(&res->params, &controller_devinfo, sizeof(controller_devinfo));
      return true;

    case CMD_GETCONDITION:
      res->header.command = CMD_RESTRANSFER;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = sizeof(ctrl->cnd) >> 2;
      memcpy(&res->params, &ctrl->cnd, sizeof(ctrl->cnd));
      return true;
  }

  return false;
}

struct maple_device *controller_create() {
  struct controller *ctrl = calloc(1, sizeof(struct controller));
  ctrl->destroy = &controller_destroy;
  ctrl->input = &controller_input;
  ctrl->frame = &controller_frame;
  ctrl->cnd.function = FN_CONTROLLER;

  /* buttons bitfield contains 0s for pressed buttons and 1s for unpressed */
  ctrl->cnd.buttons = 0xffff;

  /* triggers completely unpressed */
  ctrl->cnd.rtrig = ctrl->cnd.ltrig = 0;

  /* joysticks default to dead center */
  ctrl->cnd.joyy = ctrl->cnd.joyx = ctrl->cnd.joyx2 = ctrl->cnd.joyy2 = 0x80;

  /* default profile */
  /* CONT_JOYX */
  /* CONT_JOYY */
  /* CONT_LTRIG */
  /* CONT_RTRIG */
  ctrl->map[K_SPACE] = CONT_START;
  ctrl->map[(enum keycode)'k'] = CONT_A;
  ctrl->map[(enum keycode)'l'] = CONT_B;
  ctrl->map[(enum keycode)'j'] = CONT_X;
  ctrl->map[(enum keycode)'i'] = CONT_Y;
  ctrl->map[(enum keycode)'w'] = CONT_DPAD_UP;
  ctrl->map[(enum keycode)'s'] = CONT_DPAD_DOWN;
  ctrl->map[(enum keycode)'a'] = CONT_DPAD_LEFT;
  ctrl->map[(enum keycode)'d'] = CONT_DPAD_RIGHT;

  /* load profile */
  controller_load_profile(ctrl, OPTION_profile);

  return (struct maple_device *)ctrl;
}
