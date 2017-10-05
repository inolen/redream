#ifndef TRACER_H
#define TRACER_H

#include "host/keycode.h"

struct host;
struct tracer;
struct render_backend;

struct tracer *tracer_create(struct host *host);
void tracer_destroy(struct tracer *tracer);

void tracer_vid_created(struct tracer *tracer, struct render_backend *r);
void tracer_vid_destroyed(struct tracer *tracer);
int tracer_keydown(struct tracer *tracer, int key, int16_t value);

int tracer_load(struct tracer *tracer, const char *path);
void tracer_render_frame(struct tracer *tracer);

#endif
