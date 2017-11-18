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
  CONT_JOYX_NEG,
  CONT_JOYX_POS,
  CONT_JOYY_NEG,
  CONT_JOYY_POS,
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
  if (sub != dev) {
    return sub && sub->frame(sub, req, res);
  }

  switch (req->command) {
    case MAPLE_REQ_DEVINFO: {
      /* based on captured result of real Dreamcast controller */
      struct maple_device_info info = {0};
      info.func = MAPLE_FUNC_CONTROLLER;
      info.data[0] = 0xfe060f00;
      info.region = 0xff;
      strncpy_pad_spaces(info.name, "Dreamcast Controller", sizeof(info.name));
      strncpy_pad_spaces(
          info.license,
          "Produced By or Under License From SEGA ENTERPRISES,LTD.",
          sizeof(info.license));
      info.standby_power = 0x01ae;
      info.max_power = 0x01f4;

      res->command = MAPLE_RES_DEVINFO;
      res->dst_addr = req->src_addr;
      res->src_addr = req->dst_addr;
      res->num_words = sizeof(info) >> 2;
      memcpy(res->params, &info, sizeof(info));
    } break;

    case MAPLE_REQ_GETCOND: {
      res->command = MAPLE_RES_TRANSFER;
      res->dst_addr = req->src_addr;
      res->src_addr = req->dst_addr;
      res->num_words = sizeof(ctrl->cnd) >> 2;
      memcpy(res->params, &ctrl->cnd, sizeof(ctrl->cnd));
    } break;

    default: {
      res->command = MAPLE_RES_BADCMD;
      res->dst_addr = req->src_addr;
      res->src_addr = req->dst_addr;
      res->num_words = 0;
    } break;
  }

  /* when a primary device identifies itself in the response to a command, it
     sets the bit for each connected sub-device in addition to bit 5 */
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
                            uint16_t value) {
  struct controller *ctrl = (struct controller *)dev;

  if (button <= CONT_DPAD2_RIGHT) {
    if (value) {
      ctrl->cnd.buttons &= ~(1 << button);
    } else {
      ctrl->cnd.buttons |= (1 << button);
    }
  } else if (button == CONT_JOYX_NEG) {
    ctrl->cnd.joyx = axis_neg_u16_to_u8(value);
  } else if (button == CONT_JOYX_POS) {
    ctrl->cnd.joyx = axis_pos_u16_to_u8(value);
  } else if (button == CONT_JOYY_NEG) {
    ctrl->cnd.joyy = axis_neg_u16_to_u8(value);
  } else if (button == CONT_JOYY_POS) {
    ctrl->cnd.joyy = axis_pos_u16_to_u8(value);
  } else if (button == CONT_LTRIG) {
    ctrl->cnd.ltrig = axis_u16_to_u8(value);
  } else if (button == CONT_RTRIG) {
    ctrl->cnd.rtrig = axis_u16_to_u8(value);
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
  ctrl->cnd.function = MAPLE_FUNC_CONTROLLER;
  ctrl->cnd.buttons = 0xffff;
  ctrl->cnd.rtrig = ctrl->cnd.ltrig = 0;
  ctrl->cnd.joyy = ctrl->cnd.joyx = ctrl->cnd.joyx2 = ctrl->cnd.joyy2 = 0x80;

  return (struct maple_device *)ctrl;
}
