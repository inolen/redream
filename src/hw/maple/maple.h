#ifndef MAPLE_H
#define MAPLE_H

#include "hw/maple/maple_types.h"
#include "hw/dreamcast.h"
#include "ui/keycode.h"

struct dreamcast;
struct maple;
struct maple_device;

typedef void (*maple_destroy_cb)(struct maple_device *);
typedef bool (*maple_input_cb)(struct maple_device *, enum keycode, int16_t);
typedef bool (*maple_frame_cb)(struct maple_device *,
                               const struct maple_frame *,
                               struct maple_frame *);

struct maple_device {
  maple_destroy_cb destroy;
  maple_input_cb input;
  maple_frame_cb frame;
};

struct maple_device *controller_create();

void maple_vblank(struct maple *mp);
struct maple *maple_create(struct dreamcast *dc);
void maple_destroy(struct maple *mp);

#endif
