#ifndef TRACER_H
#define TRACER_H

struct tracer_s;
struct window_s;

void tracer_run(struct tracer_s *tracer, const char *path);

struct tracer_s *tracer_create(struct window_s *window);
void tracer_destroy(struct tracer_s *tracer);

#ifdef __cplusplus
}
#endif

#endif
