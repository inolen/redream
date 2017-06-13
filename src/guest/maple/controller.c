#include "core/option.h"
#include "core/string.h"
#include "guest/dreamcast.h"
#include "guest/maple/maple.h"

enum {
  CONT_C,
  CONT_B,
  CONT_A,
  CONT_START,
  CONT_DPAD_UP,
  CONT_DPAD_DOWN,
  CONT_DPAD_LEFT,
  CONT_DPAD_RIGHT,
  CONT_Z,
  CONT_Y,
  CONT_X,
  CONT_D,
  CONT_DPAD2_UP,
  CONT_DPAD2_DOWN,
  CONT_DPAD2_LEFT,
  CONT_DPAD2_RIGHT,
  /* only used internally, not by the real controller state */
  CONT_JOYX,
  CONT_JOYY,
  CONT_LTRIG,
  CONT_RTRIG,
  NUM_CONTROLS,
};

struct controller {
  struct maple_device;
  struct maple_cond cnd;
};

static void controller_update(struct controller *ctrl) {
  /* dc_poll_input will call into controller_input if new values are
     available */
  dc_poll_input(ctrl->dc);
}

static int controller_input(struct maple_device *dev, int button,
                            int16_t value) {
  struct controller *ctrl = (struct controller *)dev;

  if (button <= CONT_DPAD2_RIGHT) {
    if (value > 0) {
      ctrl->cnd.buttons &= ~(1 << button);
    } else {
      ctrl->cnd.buttons |= (1 << button);
    }
  } else if (button == CONT_JOYX || button == CONT_JOYY) {
    /* scale value from [INT16_MIN, INT16_MAX] to [0, UINT8_MAX] */
    uint8_t scaled = ((int32_t)value - INT16_MIN) >> 8;

    if (button == CONT_JOYX) {
      ctrl->cnd.joyx = scaled;
    } else {
      ctrl->cnd.joyy = scaled;
    }
  } else if (button == CONT_LTRIG || button == CONT_RTRIG) {
    /* scale value from [0, INT16_MAX] to [0, UINT8_MAX] */
    uint8_t scaled = (int32_t)value >> 7;

    if (button == CONT_LTRIG) {
      ctrl->cnd.ltrig = scaled;
    } else {
      ctrl->cnd.rtrig = scaled;
    }
  }

  return 1;
}

static int controller_frame(struct maple_device *dev,
                            const struct maple_frame *frame,
                            struct maple_frame *res) {
  struct controller *ctrl = (struct controller *)dev;

  switch (frame->header.command) {
    case MAPLE_REQ_DEVINFO: {
      /* based on captured result of real Dreamcast controller */
      struct maple_device_info info = {0};
      info.func = MAPLE_FUNC_CONTROLLER;
      info.data[0] = 0xfe060f00;
      info.region = 0xff;
      strncpy_spaces(info.name, "Dreamcast Controller", sizeof(info.name));
      strncpy_spaces(info.license,
                     "Produced By or Under License From SEGA ENTERPRISES,LTD.",
                     sizeof(info.license));
      info.standby_power = 0x01ae;
      info.max_power = 0x01f4;

      res->header.command = MAPLE_RES_DEVINFO;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = sizeof(info) >> 2;
      memcpy(res->params, &info, sizeof(info));
      return 1;
    }

    case MAPLE_REQ_GETCOND: {
      controller_update(ctrl);

      res->header.command = MAPLE_RES_TRANSFER;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = sizeof(ctrl->cnd) >> 2;
      memcpy(res->params, &ctrl->cnd, sizeof(ctrl->cnd));
      return 1;
    }
  }

  return 0;
}

static void controller_destroy(struct maple_device *dev) {
  struct controller *ctrl = (struct controller *)dev;
  free(ctrl);
}

struct maple_device *controller_create(struct dreamcast *dc, int port,
                                       int unit) {
  struct controller *ctrl = calloc(1, sizeof(struct controller));
  ctrl->dc = dc;
  ctrl->port = port;
  ctrl->unit = unit;
  ctrl->destroy = &controller_destroy;
  ctrl->input = &controller_input;
  ctrl->frame = &controller_frame;

  /* default state */
  ctrl->cnd.function = MAPLE_FUNC_CONTROLLER;
  ctrl->cnd.buttons = 0xffff;
  ctrl->cnd.rtrig = ctrl->cnd.ltrig = 0;
  ctrl->cnd.joyy = ctrl->cnd.joyx = ctrl->cnd.joyx2 = ctrl->cnd.joyy2 = 0x80;

  return (struct maple_device *)ctrl;
}
