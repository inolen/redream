#include "core/profiler.h"
#include "core/core.h"
#include "core/time.h"

#define PROFILER_MAX_COUNTERS 32

struct counter {
  int aggregate;
  int64_t value[2];
};

static struct {
  struct counter counters[PROFILER_MAX_COUNTERS];
  int num_counters;

  int64_t last_aggregation;
} prof;

prof_token_t prof_get_next_token() {
  prof_token_t tok = prof.num_counters++;
  CHECK_LT(tok, PROFILER_MAX_COUNTERS);
  return tok;
}

prof_token_t prof_get_counter_token(const char *name) {
  prof_token_t tok = prof_get_next_token();
  struct counter *c = &prof.counters[tok];
  c->aggregate = 0;
  return tok;
}

prof_token_t prof_get_aggregate_token(const char *name) {
  prof_token_t tok = prof_get_next_token();
  struct counter *c = &prof.counters[tok];
  c->aggregate = 1;
  return tok;
}

void prof_flip(int64_t now) {
  /* update time-based aggregate counters every second */
  int64_t next_aggregation = prof.last_aggregation + NS_PER_SEC;

  if (now > next_aggregation) {
    for (int i = 0; i < PROFILER_MAX_COUNTERS; i++) {
      struct counter *c = &prof.counters[i];

      if (c->aggregate) {
        c->value[0] = c->value[1];
        c->value[1] = 0;
      }
    }

    prof.last_aggregation = now;
  }
}

void prof_counter_set(prof_token_t tok, int64_t count) {
  struct counter *c = &prof.counters[tok];
  c->value[1] = count;
}

void prof_counter_add(prof_token_t tok, int64_t count) {
  struct counter *c = &prof.counters[tok];
  c->value[1] += count;
}

int64_t prof_counter_load(prof_token_t tok) {
  struct counter *c = &prof.counters[tok];
  if (c->aggregate) {
    /* return the last aggregated value */
    return c->value[0];
  } else {
    return c->value[1];
  }
}
