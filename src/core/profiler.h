#ifndef PROFILER_H
#define PROFILER_H

#include <stdint.h>

typedef uint64_t prof_token_t;

#define PROF_ENTER(group, name)             \
  static int prof_init = 0;                 \
  static prof_token_t prof_tok;             \
  if (!prof_init) {                         \
    prof_tok = prof_get_token(group, name); \
    prof_init = 1;                          \
  }                                         \
  uint64_t prof_tick = prof_enter(prof_tok)

#define PROF_LEAVE() prof_leave(prof_tok, prof_tick)

#define PROF_COUNT(name, count)              \
  {                                          \
    static int prof_init = 0;                \
    static prof_token_t prof_tok;            \
    if (!prof_init) {                        \
      prof_tok = prof_get_count_token(name); \
      prof_init = 1;                         \
    }                                        \
    prof_count(tok, count);                  \
  }

prof_token_t prof_get_token(const char *group, const char *name);
prof_token_t prof_get_count_token(const char *name);

uint64_t prof_enter(prof_token_t tok);
void prof_leave(prof_token_t tok, uint64_t tick);
void prof_count(prof_token_t tok, int count);

#endif
