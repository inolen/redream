#ifndef MICROPROFILE_IMPL_H
#define MICROPROFILE_IMPL_H

#include "host/keycode.h"

struct microprofile;
struct render_backend;

struct microprofile *mp_create(struct render_backend *r);
void mp_destroy(struct microprofile *mp);

void mp_keydown(struct microprofile *mp, enum keycode key, int16_t value);
void mp_mousemove(struct microprofile *mp, int x, int y);

void mp_render(struct microprofile *mp);

#endif
