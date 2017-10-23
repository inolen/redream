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

struct scheduler *sched_create(struct dreamcast *dc);
void sched_destroy(struct scheduler *sch);

void sched_tick(struct scheduler *sch, int64_t ns);

struct timer *sched_start_timer(struct scheduler *sch, timer_cb cb, void *data,
                                int64_t ns);
int64_t sched_remaining_time(struct scheduler *sch, struct timer *);
void sched_cancel_timer(struct scheduler *sch, struct timer *);

#endif
