#include <stdio.h>
#include "guest/scheduler.h"
#include "core/core.h"
#include "core/list.h"
#include "guest/dreamcast.h"

#define MAX_TIMERS 128

struct timer {
  int active;
  int64_t expire;
  timer_cb cb;
  void *data;
  struct list_node it;
};

struct scheduler {
  struct dreamcast *dc;
  struct timer timers[MAX_TIMERS];
  struct list free_timers;
  struct list live_timers;
  int64_t base_time;
};

void sched_cancel_timer(struct scheduler *sched, struct timer *timer) {
  if (!timer->active) {
    return;
  }

  timer->active = 0;
  list_remove(&sched->live_timers, &timer->it);
  list_add(&sched->free_timers, &timer->it);
}

int64_t sched_remaining_time(struct scheduler *sched, struct timer *timer) {
  return timer->expire - sched->base_time;
}

struct timer *sched_start_timer(struct scheduler *sched, timer_cb cb,
                                void *data, int64_t ns) {
  struct timer *timer = list_first_entry(&sched->free_timers, struct timer, it);
  CHECK_NOTNULL(timer);
  timer->active = 1;
  timer->expire = sched->base_time + ns;
  timer->cb = cb;
  timer->data = data;

  /* remove from free list */
  list_remove(&sched->free_timers, &timer->it);

  /* add to sorted live list */
  struct list_node *after = NULL;

  list_for_each(&sched->live_timers, it) {
    struct timer *entry = list_entry(it, struct timer, it);

    if (entry->expire > timer->expire) {
      break;
    }

    after = it;
  }

  list_add_after(&sched->live_timers, after, &timer->it);

  return timer;
}

void sched_tick(struct scheduler *sched, int64_t ns) {
  int64_t target_time = sched->base_time + ns;

  while (sched->dc->running && sched->base_time < target_time) {
    /* run devices up to the next timer */
    int64_t next_time = target_time;
    struct timer *next_timer =
        list_first_entry(&sched->live_timers, struct timer, it);

    if (next_timer && next_timer->expire < next_time) {
      next_time = next_timer->expire;
    }

    /* update base time before running devices and expiring timers in case one
       of them schedules a new timer */
    int64_t slice = next_time - sched->base_time;
    sched->base_time += slice;

    /* execute each device */
    list_for_each_entry(dev, &sched->dc->devices, struct device, it) {
      if (dev->runif.enabled && dev->runif.running) {
        dev->runif.run(dev, slice);
      }
    }

    /* execute expired timers */
    while (1) {
      struct timer *timer =
          list_first_entry(&sched->live_timers, struct timer, it);

      if (!timer || timer->expire > sched->base_time) {
        break;
      }

      sched_cancel_timer(sched, timer);

      /* run the timer */
      timer->cb(timer->data);
    }
  }
}

void sched_destroy(struct scheduler *sch) {
  free(sch);
}

struct scheduler *sched_create(struct dreamcast *dc) {
  struct scheduler *sched = calloc(1, sizeof(struct scheduler));

  sched->dc = dc;

  /* add all timers to the free list initially */
  for (int i = 0; i < MAX_TIMERS; i++) {
    struct timer *timer = &sched->timers[i];
    list_add(&sched->free_timers, &timer->it);
  }

  return sched;
}
