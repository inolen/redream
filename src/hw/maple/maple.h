#ifndef MAPLE_H
#define MAPLE_H

#include "hw/dreamcast.h"
#include "hw/maple/maple_types.h"
#include "ui/keycode.h"

struct dreamcast;
struct maple;
struct maple_device;

struct maple_device {
  int port;
  int unit;
  void (*destroy)(struct maple_device *);
  int (*input)(struct maple_device *, enum keycode, int16_t);
  int (*frame)(struct maple_device *, const struct maple_frame *,
               struct maple_frame *);
};

struct maple *maple_create(struct dreamcast *dc);
void maple_destroy(struct maple *mp);

int maple_handle_command(struct maple *mp, struct maple_frame *frame,
                         struct maple_frame *res);

struct maple_device *controller_create();
struct maple_device *vmu_create();

#endif
