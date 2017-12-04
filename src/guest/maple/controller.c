#include "core/core.h"
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

static int controller_frame(struct maple_device *dev,
                            const union maple_frame *req,
                            union maple_frame *res) {
  struct controller *ctrl = (struct controller *)dev;

  /* forward to sub-device if specified */
  int port, unit;
  maple_decode_addr(req->dst_addr, &port, &unit);

  struct maple_device *sub = maple_get_device(dev->mp, port, unit);
  if (!sub) {
    return 0;
  }

  if (sub != dev) {
    return sub->frame(sub, req, res);
  }

  switch (req->cmd) {
    case MAPLE_REQ_DEVINFO: {
      char *name = "Dreamcast Controller";
      char *license = "Produced By or Under License From SEGA ENTERPRISES,LTD.";
      struct maple_device_info info = {0};
      info.func = MAPLE_FUNC_CONTROLLER;
      info.data[0] = 0xfe060f00;
      info.region = 0xff;
      strncpy_pad_spaces(info.name, name, sizeof(info.name));
      strncpy_pad_spaces(info.license, license, sizeof(info.license));
      info.standby_power = 0x01ae;
      info.max_power = 0x01f4;

      res->cmd = MAPLE_RES_DEVINFO;
      res->num_words = sizeof(info) >> 2;
      memcpy(res->params, &info, sizeof(info));
    } break;

    case MAPLE_REQ_GETCOND: {
      res->cmd = MAPLE_RES_TRANSFER;
      res->num_words = sizeof(ctrl->cnd) >> 2;
      memcpy(res->params, &ctrl->cnd, sizeof(ctrl->cnd));
    } break;

    default:
      res->cmd = MAPLE_RES_BADCMD;
      break;
  }

  /* when a primary device identifies itself in the response to a cmd, it sets
     the bit for each connected sub-device in addition to bit 5 */
  for (int i = 0; i < MAPLE_MAX_UNITS - 1; i++) {
    struct maple_device *sub = maple_get_device(dev->mp, port, i);

    if (!sub) {
      continue;
    }

    res->src_addr |= 1 << i;
  }

  return 1;
}

static int controller_input(struct maple_device *dev, int button,
                            int16_t value) {
  struct controller *ctrl = (struct controller *)dev;

  if (button <= CONT_DPAD2_RIGHT) {
    if (value) {
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

static void controller_destroy(struct maple_device *dev) {
  struct controller *ctrl = (struct controller *)dev;
  free(ctrl);
}

struct maple_device *controller_create(struct maple *mp, int port) {
  struct controller *ctrl = calloc(1, sizeof(struct controller));

  ctrl->mp = mp;
  ctrl->destroy = &controller_destroy;
  ctrl->input = &controller_input;
  ctrl->frame = &controller_frame;

  /* default state */
  ctrl->cnd.func = MAPLE_FUNC_CONTROLLER;
  ctrl->cnd.buttons = 0xffff;
  ctrl->cnd.rtrig = ctrl->cnd.ltrig = 0;
  ctrl->cnd.joyy = ctrl->cnd.joyx = ctrl->cnd.joyx2 = ctrl->cnd.joyy2 = 0x80;

  return (struct maple_device *)ctrl;
}
