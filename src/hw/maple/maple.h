#ifndef MAPLE_H
#define MAPLE_H

#include "hw/maple/maple_types.h"
#include "hw/dreamcast.h"
#include "ui/keycode.h"

struct dreamcast_s;
struct maple_s;
struct maple_device_s;

typedef void (*maple_destroy_cb)(struct maple_device_s *);
typedef bool (*maple_input_cb)(struct maple_device_s *, keycode_t, int16_t);
typedef bool (*maple_frame_cb)(struct maple_device_s *, const maple_frame_t *,
                               maple_frame_t *);

typedef struct maple_device_s {
  maple_destroy_cb destroy;
  maple_input_cb input;
  maple_frame_cb frame;
} maple_device_t;

struct maple_device_s *controller_create();

void maple_vblank(struct maple_s *mp);

struct maple_s *maple_create(struct dreamcast_s *dc);
void maple_destroy(struct maple_s *mp);

#endif
