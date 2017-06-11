#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "core/time.h"

struct dreamcast;
struct timer;
struct scheduler;

#define HZ_TO_NANO(hz) (int64_t)(NS_PER_SEC / (float)(hz))
#define NANO_TO_CYCLES(ns, hz) (int64_t)(((ns) / (float)NS_PER_SEC) * (hz))
#define CYCLES_TO_NANO(cycles, hz) \
  (int64_t)(((cycles) / (float)(hz)) * NS_PER_SEC)

typedef void (*timer_cb)(void *);

struct scheduler *scheduler_create(struct dreamcast *dc);
void scheduler_destroy(struct scheduler *sch);

void scheduler_tick(struct scheduler *sch, int64_t ns);

struct timer *scheduler_start_timer(struct scheduler *sch, timer_cb cb,
                                    void *data, int64_t ns);
int64_t scheduler_remaining_time(struct scheduler *sch, struct timer *);
void scheduler_cancel_timer(struct scheduler *sch, struct timer *);

#endif
