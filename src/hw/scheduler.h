#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

struct dreamcast_s;
struct timer_s;
struct scheduler_s;

static const int64_t NS_PER_SEC = 1000000000ll;

#define HZ_TO_NANO(hz) (int64_t)(NS_PER_SEC / (float)hz)
#define NANO_TO_CYCLES(ns, hz) (int64_t)((ns / (float)NS_PER_SEC) * hz)
#define CYCLES_TO_NANO(cycles, hz) (int64_t)((cycles / (float)hz) * NS_PER_SEC)

typedef void (*timer_cb)(void *);

void scheduler_tick(struct scheduler_s *sch, int64_t ns);
struct timer_s *scheduler_start_timer(struct scheduler_s *sch, timer_cb cb,
                                      void *data, int64_t ns);
int64_t scheduler_remaining_time(struct scheduler_s *sch, struct timer_s *);
void scheduler_cancel_timer(struct scheduler_s *sch, struct timer_s *);

struct scheduler_s *scheduler_create(struct dreamcast_s *dc);
void scheduler_destroy(struct scheduler_s *sch);

#endif
