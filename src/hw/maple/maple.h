#ifndef MAPLE_H
#define MAPLE_H

#include "hw/dreamcast.h"
#include "hw/maple/maple_types.h"

struct dreamcast;
struct maple;
struct maple_device;

struct maple_device {
  struct dreamcast *dc;
  int port;
  int unit;
  void (*destroy)(struct maple_device *);
  int (*frame)(struct maple_device *, const struct maple_frame *,
               struct maple_frame *);
};

struct maple *maple_create(struct dreamcast *dc);
void maple_destroy(struct maple *mp);

int maple_handle_command(struct maple *mp, struct maple_frame *frame,
                         struct maple_frame *res);

struct maple_device *controller_create(struct dreamcast *dc, int port,
                                       int unit);
struct maple_device *vmu_create(struct dreamcast *dc, int port, int unit);

#endif
