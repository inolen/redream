#ifndef TRACER_H
#define TRACER_H

struct host;
struct tracer;
struct render_backend;

struct tracer *tracer_create(struct host *host, struct render_backend *);
void tracer_destroy(struct tracer *tracer);

int tracer_load(struct tracer *tracer, const char *path);
void tracer_render_frame(struct tracer *tracer, int width, int height);

#endif
