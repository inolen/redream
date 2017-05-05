#ifndef TRACER_H
#define TRACER_H

struct host;
struct tracer;

struct tracer *tracer_create(struct host *host);
void tracer_destroy(struct tracer *tracer);

int tracer_load(struct tracer *tracer, const char *path);
void tracer_run(struct tracer *tracer);

#endif
