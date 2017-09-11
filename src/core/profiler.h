#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>
#include "core/constructor.h"
#include "core/list.h"

typedef int prof_token_t;

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

prof_token_t prof_get_token(const char *group, const char *name);
prof_token_t prof_get_counter_token(const char *name);
prof_token_t prof_get_aggregate_token(const char *name);

int64_t prof_counter_load(prof_token_t tok);
void prof_counter_add(prof_token_t tok, int64_t count);
void prof_counter_set(prof_token_t tok, int64_t count);

void prof_flip(int64_t now);

#endif
