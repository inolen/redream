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
  int (*input)(struct maple_device *, int, int16_t);
  int (*frame)(struct maple_device *, const struct maple_frame *,
               struct maple_frame *);
};

/* strings passed as responses to maple queries are not null-terminated, and
   instead padded with spaces to their maximum width */
static inline void maple_strncpy(char *dst, const char *str, int size) {
  int len = (int)strlen(str);
  size -= len;
  while (len--) {
    *(dst++) = *(str++);
  }
  while (size--) {
    *(dst++) = ' ';
  }
}

struct maple *maple_create(struct dreamcast *dc);
void maple_destroy(struct maple *mp);

void maple_handle_input(struct maple *mp, int port, int button, int16_t value);
int maple_handle_command(struct maple *mp, struct maple_frame *frame,
                         struct maple_frame *res);

struct maple_device *controller_create(struct dreamcast *dc, int port,
                                       int unit);
struct maple_device *vmu_create(struct dreamcast *dc, int port, int unit);

#endif
