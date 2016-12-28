#ifndef TRACER_H
#define TRACER_H

struct tracer;
struct window;

struct tracer *tracer_create(struct window *window);
void tracer_destroy(struct tracer *tracer);

void tracer_run(struct tracer *tracer, const char *path);

#endif
