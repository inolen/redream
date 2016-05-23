#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dreamcast_s;
struct timer_s;
struct scheduler_s;

typedef void (*timer_cb)(void *);

static const int64_t NS_PER_SEC = 1000000000ll;

static inline int64_t HZ_TO_NANO(int64_t hz) {
  float nano = NS_PER_SEC / (float)hz;
  return (int64_t)nano;
}

static inline int64_t NANO_TO_CYCLES(int64_t ns, int64_t hz) {
  float cycles = (ns / (float)NS_PER_SEC) * hz;
  return (int64_t)cycles;
}

static inline int64_t CYCLES_TO_NANO(int64_t cycles, int64_t hz) {
  float nano = (cycles / (float)hz) * NS_PER_SEC;
  return (int64_t)nano;
}

struct scheduler_s *scheduler_create(struct dreamcast_s *dc);
void scheduler_destroy(struct scheduler_s *sch);
void scheduler_tick(struct scheduler_s *sch, int64_t ns);
struct timer_s *scheduler_start_timer(struct scheduler_s *sch, timer_cb cb,
                                      void *data, int64_t ns);
int64_t scheduler_remaining_time(struct scheduler_s *sch, struct timer_s *);
void scheduler_cancel_timer(struct scheduler_s *sch, struct timer_s *);

#ifdef __cplusplus
}
#endif

#endif
