#ifndef TRACER_H
#define TRACER_H

struct tracer_s;
struct window;

void tracer_run(struct tracer_s *tracer, const char *path);

struct tracer_s *tracer_create(struct window *window);
void tracer_destroy(struct tracer_s *tracer);

#ifdef __cplusplus
}
#endif

#endif
