#ifndef MAPLE_H
#define MAPLE_H

#include "hw/dreamcast.h"
#include "hw/maple/maple_types.h"
#include "ui/keycode.h"

struct dreamcast;
struct maple;
struct maple_device;

struct maple_device {
  void (*destroy)(struct maple_device *);
  bool (*input)(struct maple_device *, enum keycode, int16_t);
  bool (*frame)(struct maple_device *, const struct maple_frame *,
                struct maple_frame *);
};

struct maple_device *controller_create();

int maple_handle_command(struct maple *mp, int port, struct maple_frame *frame,
                         struct maple_frame *res);

struct maple *maple_create(struct dreamcast *dc);
void maple_destroy(struct maple *mp);

#endif
