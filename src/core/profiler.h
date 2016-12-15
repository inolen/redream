#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include "core/constructor.h"
#include "core/list.h"

typedef uint64_t prof_token_t;

#define DECLARE_COUNTER(name) extern prof_token_t COUNTER_##name;

#define DEFINE_COUNTER(name)                        \
  prof_token_t COUNTER_##name;                      \
  CONSTRUCTOR(COUNTER_REGISTER_##name) {            \
    COUNTER_##name = prof_get_counter_token(#name); \
  }

/* aggregate counters are flipped every second to show the count per-second */
#define DEFINE_AGGREGATE_COUNTER(name)                \
  prof_token_t COUNTER_##name;                        \
  CONSTRUCTOR(COUNTER_REGISTER_##name) {              \
    COUNTER_##name = prof_get_aggregate_token(#name); \
  }

#define PROF_ENTER(group, name)             \
  static int prof_init = 0;                 \
  static prof_token_t prof_tok;             \
  if (!prof_init) {                         \
    prof_tok = prof_get_token(group, name); \
    prof_init = 1;                          \
  }                                         \
  uint64_t prof_tick = prof_enter(prof_tok)

#define PROF_LEAVE() prof_leave(prof_tok, prof_tick)

prof_token_t prof_get_token(const char *group, const char *name);
prof_token_t prof_get_counter_token(const char *name);
prof_token_t prof_get_aggregate_token(const char *name);

void prof_init();
void prof_shutdown();

uint64_t prof_enter(prof_token_t tok);
void prof_leave(prof_token_t tok, uint64_t tick);

int64_t prof_counter_load(prof_token_t tok);
void prof_counter_add(prof_token_t tok, int64_t count);
void prof_counter_set(prof_token_t tok, int64_t count);

/* called at the end of every frame to aggregate frame-based profile zones */
void prof_flip();

/* called periodically to aggregate time-based aggregate counters */
void prof_update(int64_t now);

#endif
