#include <atomic>

#if ENABLE_MICROPROFILE
#include <microprofile.h>
#endif

extern "C" {

#include "core/profiler.h"
#include "core/time.h"

static struct {
#if ENABLE_MICROPROFILE
  std::atomic<int64_t> counters[MICROPROFILE_MAX_COUNTERS];
  int aggregate[MICROPROFILE_MAX_COUNTERS];
  int64_t last_aggregation;
#else
/* avoid having an empty struct in c (only valid through GCC extension) */
  int dummy;
#endif
} prof;

static inline float hue_to_rgb(float p, float q, float t) {
  if (t < 0.0f) {
    t += 1.0f;
  }
  if (t > 1.0f) {
    t -= 1.0f;
  }
  if (t < 1.0f / 6.0f) {
    return p + (q - p) * 6.0f * t;
  }
  if (t < 1.0f / 2.0f) {
    return q;
  }
  if (t < 2.0f / 3.0f) {
    return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
  }
  return p;
}

static inline void hsl_to_rgb(float h, float s, float l, uint8_t *r, uint8_t *g,
                              uint8_t *b) {
  float fr, fg, fb;

  if (s == 0.0f) {
    fr = fg = fb = l;
  } else {
    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    fr = hue_to_rgb(p, q, h + 1.0f / 3.0f);
    fg = hue_to_rgb(p, q, h);
    fb = hue_to_rgb(p, q, h - 1.0f / 3.0f);
  }

  *r = (uint8_t)(fr * 255);
  *g = (uint8_t)(fg * 255);
  *b = (uint8_t)(fb * 255);
}

static unsigned prof_hash(const char *name) {
  unsigned hash = 5381;
  char c;
  while ((c = *name++)) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}

static uint32_t prof_scope_color(const char *name) {
  unsigned name_hash = prof_hash(name);
  float h = (name_hash % 360) / 360.0f;
  float s = 0.7f;
  float l = 0.6f;
  uint8_t r, g, b;
  hsl_to_rgb(h, s, l, &r, &g, &b);
  return (r << 16) | (g << 8) | b;
}

prof_token_t prof_get_token(const char *group, const char *name) {
  prof_init();

#if ENABLE_MICROPROFILE
  uint32_t color = prof_scope_color(name);
  return MicroProfileGetToken(group, name, color, MicroProfileTokenTypeCpu);
#else
  return 0;
#endif
}

prof_token_t prof_get_counter_token(const char *name) {
  prof_init();

#if ENABLE_MICROPROFILE
  prof_token_t tok = MicroProfileGetCounterToken(name);
  prof.aggregate[tok] = 0;
  return tok;
#else
  return 0;
#endif
}

prof_token_t prof_get_aggregate_token(const char *name) {
  prof_init();
#if ENABLE_MICROPROFILE
  prof_token_t tok = MicroProfileGetCounterToken(name);
  prof.aggregate[tok] = 1;
  return tok;
#else
  return 0;
#endif
}

void prof_flip() {
/* flip frame-based profile zones at the end of every frame */
#if ENABLE_MICROPROFILE
  MicroProfileFlip();
#endif
}

void prof_update(int64_t now) {
/* update time-based aggregate counters every second */
#if ENABLE_MICROPROFILE
  int64_t next_aggregation = prof.last_aggregation + NS_PER_SEC;

  if (now > next_aggregation) {
    for (int i = 0; i < MICROPROFILE_MAX_COUNTERS; i++) {
      if (!prof.aggregate[i]) {
        continue;
      }

      MicroProfileCounterSet(i, prof.counters[i].load());
      prof.counters[i].store(0);
    }

    prof.last_aggregation = now;
  }
#endif
}

void prof_counter_set(prof_token_t tok, int64_t count) {
#if ENABLE_MICROPROFILE
  if (prof.aggregate[tok]) {
    prof.counters[tok].store(count);
  } else {
    MicroProfileCounterSet(tok, count);
  }
#endif
}

void prof_counter_add(prof_token_t tok, int64_t count) {
#if ENABLE_MICROPROFILE
  if (prof.aggregate[tok]) {
    prof.counters[tok].fetch_add(count);
  } else {
    MicroProfileCounterAdd(tok, count);
  }
#endif
}

int64_t prof_counter_load(prof_token_t tok) {
#if ENABLE_MICROPROFILE
  return MicroProfileCounterLoad(tok);
#else
  return 0;
#endif
}

void prof_leave(prof_token_t tok, uint64_t tick) {
#if ENABLE_MICROPROFILE
  MicroProfileLeave(tok, tick);
#endif
}

uint64_t prof_enter(prof_token_t tok) {
#if ENABLE_MICROPROFILE
  return MicroProfileEnter(tok);
#else
  return 0;
#endif
}

void prof_shutdown() {
#if ENABLE_MICROPROFILE
  MicroProfileShutdown();
#endif
}

void prof_init() {
#if ENABLE_MICROPROFILE
  MicroProfileInit();
#endif
}
}
