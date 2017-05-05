#include "core/option.h"
#include "core/string.h"
#include "hw/maple/maple.h"

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

  /* scale incoming int16_t -> uint8_t */
  uint8_t scaled = ((int32_t)value - INT16_MIN) >> 8;

  if (button <= CONT_DPAD2_RIGHT) {
    if (value > 0) {
      ctrl->cnd.buttons &= ~(1 << button);
    } else {
      ctrl->cnd.buttons |= (1 << button);
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

  return 1;
}

static int controller_frame(struct maple_device *dev,
                            const struct maple_frame *frame,
                            struct maple_frame *res) {
  struct controller *ctrl = (struct controller *)dev;

  switch (frame->header.command) {
    case MAPLE_REQ_DEVINFO: {
      static struct maple_device_info controller_devinfo = {
          MAPLE_FUNC_CONTROLLER,
          {0xfe060f00, 0x0, 0x0},
          0xff,
          0,
          "Dreamcast Controller",
          "Produced By or Under License From SEGA ENTERPRISES,LTD.",
          0x01ae,
          0x01f4};

      res->header.command = MAPLE_RES_DEVINFO;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = sizeof(controller_devinfo) >> 2;
      memcpy(res->params, &controller_devinfo, sizeof(controller_devinfo));
    }
      return 1;

    case MAPLE_REQ_GETCOND: {
      controller_update(ctrl);

      res->header.command = MAPLE_RES_TRANSFER;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = sizeof(ctrl->cnd) >> 2;
      memcpy(res->params, &ctrl->cnd, sizeof(ctrl->cnd));
    }
      return 1;
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
