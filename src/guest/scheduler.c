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

void scheduler_cancel_timer(struct scheduler *sch, struct timer *timer) {
  if (!timer->active) {
    return;
  }

  timer->active = 0;
  list_remove(&sch->live_timers, &timer->it);
  list_add(&sch->free_timers, &timer->it);
}

int64_t scheduler_remaining_time(struct scheduler *sch, struct timer *timer) {
  return timer->expire - sch->base_time;
}

struct timer *scheduler_start_timer(struct scheduler *sch, timer_cb cb,
                                    void *data, int64_t ns) {
  struct timer *timer = list_first_entry(&sch->free_timers, struct timer, it);
  CHECK_NOTNULL(timer);
  timer->active = 1;
  timer->expire = sch->base_time + ns;
  timer->cb = cb;
  timer->data = data;

  /* remove from free list */
  list_remove(&sch->free_timers, &timer->it);

  /* add to sorted live list */
  struct list_node *after = NULL;

  list_for_each(&sch->live_timers, it) {
    struct timer *entry = list_entry(it, struct timer, it);

    if (entry->expire > timer->expire) {
      break;
    }

    after = it;
  }

  list_add_after(&sch->live_timers, after, &timer->it);

  return timer;
}

void scheduler_tick(struct scheduler *sch, int64_t ns) {
  int64_t target_time = sch->base_time + ns;

  while (sch->dc->running && sch->base_time < target_time) {
    /* run devices up to the next timer */
    int64_t next_time = target_time;
    struct timer *next_timer =
        list_first_entry(&sch->live_timers, struct timer, it);

    if (next_timer && next_timer->expire < next_time) {
      next_time = next_timer->expire;
    }

    /* update base time before running devices and expiring timers in case one
       of them schedules a new timer */
    int64_t slice = next_time - sch->base_time;
    sch->base_time += slice;

    /* execute each device */
    list_for_each_entry(dev, &sch->dc->devices, struct device, it) {
      if (dev->execute_if && dev->execute_if->running) {
        dev->execute_if->run(dev, slice);
      }
    }

    /* execute expired timers */
    while (1) {
      struct timer *timer =
          list_first_entry(&sch->live_timers, struct timer, it);

      if (!timer || timer->expire > sch->base_time) {
        break;
      }

      scheduler_cancel_timer(sch, timer);

      /* run the timer */
      timer->cb(timer->data);
    }
  }
}

void scheduler_destroy(struct scheduler *sch) {
  free(sch);
}

struct scheduler *scheduler_create(struct dreamcast *dc) {
  struct scheduler *sch = calloc(1, sizeof(struct scheduler));

  sch->dc = dc;

  /* add all timers to the free list initially */
  for (int i = 0; i < MAX_TIMERS; i++) {
    struct timer *timer = &sch->timers[i];

    list_add(&sch->free_timers, &timer->it);
  }

  return sch;
}
