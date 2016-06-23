#ifndef TRACER_H
#define TRACER_H

struct tracer;
struct window;

void tracer_run(struct tracer *tracer, const char *path);

struct tracer *tracer_create(struct window *window);
void tracer_destroy(struct tracer *tracer);

#endif
