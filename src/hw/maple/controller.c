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
  // only used by internal button map
  CONT_JOYX = 0x10000,
  CONT_JOYY = 0x20000,
  CONT_LTRIG = 0x40000,
  CONT_RTRIG = 0x80000
};

typedef struct {
  uint32_t function;
  uint16_t buttons;
  uint8_t rtrig;
  uint8_t ltrig;
  uint8_t joyx;
  uint8_t joyy;
  uint8_t joyx2;
  uint8_t joyy2;
} condition_t;

typedef struct {
  maple_device_t base;
  condition_t cnd;
  int map[K_NUM_KEYS];
} controller_t;

// Constant device info structure sent as response to CMD_REQDEVINFO to
// identify the controller.
static maple_deviceinfo_t controller_devinfo = {
    FN_CONTROLLER,
    {0xfe060f00, 0x0, 0x0},
    0xff,
    0,
    "Dreamcast Controller",
    "Produced By or Under License From SEGA ENTERPRISES,LTD.",
    0x01ae,
    0x01f4};

static void controller_load_profile(controller_t *ctrl, const char *path);
static int controller_ini_handler(void *user, const char *section,
                                  const char *name, const char *value);
static void controller_destroy(controller_t *controller);
static bool controller_input(controller_t *ctrl, keycode_t key, int16_t value);
static bool controller_frame(controller_t *ctrl, const maple_frame_t *frame,
                             maple_frame_t *res);

maple_device_t *controller_create() {
  controller_t *ctrl = calloc(1, sizeof(controller_t));
  ctrl->base.destroy = (maple_destroy_cb)&controller_destroy;
  ctrl->base.input = (maple_input_cb)&controller_input;
  ctrl->base.frame = (maple_frame_cb)&controller_frame;
  ctrl->cnd.function = FN_CONTROLLER;

  // buttons bitfield contains 0s for pressed buttons and 1s for unpressed
  ctrl->cnd.buttons = 0xffff;

  // triggers completely unpressed
  ctrl->cnd.rtrig = ctrl->cnd.ltrig = 0;

  // joysticks default to dead center
  ctrl->cnd.joyy = ctrl->cnd.joyx = ctrl->cnd.joyx2 = ctrl->cnd.joyy2 = 0x80;

  // default profile
  // CONT_JOYX
  // CONT_JOYY
  // CONT_LTRIG
  // CONT_RTRIG
  ctrl->map[K_SPACE] = CONT_START;
  ctrl->map[(keycode_t)'k'] = CONT_A;
  ctrl->map[(keycode_t)'l'] = CONT_B;
  ctrl->map[(keycode_t)'j'] = CONT_X;
  ctrl->map[(keycode_t)'i'] = CONT_Y;
  ctrl->map[(keycode_t)'w'] = CONT_DPAD_UP;
  ctrl->map[(keycode_t)'s'] = CONT_DPAD_DOWN;
  ctrl->map[(keycode_t)'a'] = CONT_DPAD_LEFT;
  ctrl->map[(keycode_t)'d'] = CONT_DPAD_RIGHT;

  // load profile
  controller_load_profile(ctrl, OPTION_profile);

  return &ctrl->base;
}

static void controller_load_profile(controller_t *ctrl, const char *path) {
  if (!*path) {
    return;
  }

  LOG_INFO("Loading controller profile %s", path);

  if (ini_parse(path, controller_ini_handler, ctrl) < 0) {
    LOG_WARNING("Failed to parse %s", path);
    return;
  }
}

static int controller_ini_handler(void *user, const char *section,
                                  const char *name, const char *value) {
  controller_t *ctrl = user;

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

  keycode_t key = get_key_by_name(value);
  if (key == K_UNKNOWN) {
    LOG_WARNING("Unknown key %s", value);
    return 0;
  }

  ctrl->map[key] = button;

  return 1;
}

static void controller_destroy(controller_t *controller) {
  free(controller);
}

static bool controller_input(controller_t *ctrl, keycode_t key, int16_t value) {
  // map incoming key to dreamcast button
  int button = ctrl->map[key];

  // scale incoming int16_t -> uint8_t
  uint8_t scaled = ((int32_t)value - INT16_MIN) >> 8;

  if (!button) {
    LOG_WARNING("Ignored key %s, no mapping found", get_name_by_key(key));
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

static bool controller_frame(controller_t *ctrl, const maple_frame_t *frame,
                             maple_frame_t *res) {
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
