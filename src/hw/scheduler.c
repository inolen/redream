#include <stdio.h>
#include "core/assert.h"
#include "core/core.h"
#include "core/list.h"
#include "hw/dreamcast.h"
#include "hw/scheduler.h"

static const int MAX_TIMERS = 128;

typedef struct timer_s {
  int64_t expire;
  timer_cb cb;
  void *data;
  list_node_t it;
} timer_t;

typedef struct scheduler_s {
  struct dreamcast_s *dc;
  timer_t timers[MAX_TIMERS];
  list_t free_timers;
  list_t live_timers;
  int64_t base_time;
} scheduler_t;

void scheduler_tick(scheduler_t *sch, int64_t ns) {
  int64_t target_time = sch->base_time + ns;

  while (sch->base_time < target_time) {
    if (sch->dc->suspended) {
      break;
    }

    // run devices up to the next timer
    int64_t next_time = target_time;
    timer_t *next_timer = list_first_entry(&sch->live_timers, timer_t, it);

    if (next_timer && next_timer->expire < next_time) {
      next_time = next_timer->expire;
    }

    // go ahead and update base time before running devices and expiring timers
    // in case one of them schedules a new timer
    int64_t slice = next_time - sch->base_time;
    sch->base_time += slice;

    // execute each device
    device_t *dev = sch->dc->devices;

    while (dev) {
      if (dev->execute && !dev->execute->suspended) {
        dev->execute->run(dev, slice);
      }

      dev = dev->next;
    }

    // execute expired timers
    while (1) {
      timer_t *timer = list_first_entry(&sch->live_timers, timer_t, it);

      if (!timer || timer->expire > sch->base_time) {
        break;
      }

      scheduler_cancel_timer(sch, timer);

      // run the timer
      timer->cb(timer->data);
    }
  }
}

timer_t *scheduler_start_timer(scheduler_t *sch, timer_cb cb, void *data,
                               int64_t ns) {
  timer_t *timer = list_first_entry(&sch->free_timers, timer_t, it);
  CHECK_NOTNULL(timer);
  timer->expire = sch->base_time + ns;
  timer->cb = cb;
  timer->data = data;

  // remove from free list
  list_remove(&sch->free_timers, &timer->it);

  // add to sorted live list
  list_node_t *after = NULL;

  list_for_each(&sch->live_timers, it) {
    timer_t *entry = list_entry(it, timer_t, it);

    if (entry->expire > timer->expire) {
      break;
    }

    after = it;
  }

  list_add_after(&sch->live_timers, after, &timer->it);

  return timer;
}

int64_t scheduler_remaining_time(scheduler_t *sch, timer_t *timer) {
  return timer->expire - sch->base_time;
}

void scheduler_cancel_timer(scheduler_t *sch, timer_t *timer) {
  list_remove(&sch->live_timers, &timer->it);

  list_add(&sch->free_timers, &timer->it);
}

scheduler_t *scheduler_create(dreamcast_t *dc) {
  scheduler_t *sch = calloc(1, sizeof(scheduler_t));

  sch->dc = dc;

  // add all timers to the free list initially
  for (int i = 0; i < MAX_TIMERS; i++) {
    timer_t *timer = &sch->timers[i];

    list_add(&sch->free_timers, &timer->it);
  }

  return sch;
}

void scheduler_destroy(scheduler_t *sch) {
  free(sch);
}
