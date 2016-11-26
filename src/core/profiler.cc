#include <microprofile.h>

extern "C" {

#include "core/profiler.h"
#include "sys/time.h"

static struct list prof_stats;
static int64_t last_update;

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
  uint32_t color = prof_scope_color(name);
  return MicroProfileGetToken(group, name, color, MicroProfileTokenTypeCpu);
}

prof_token_t prof_get_count_token(const char *name) {
  return MicroProfileGetCounterToken(name);
}

uint64_t prof_enter(prof_token_t tok) {
  return MicroProfileEnter(tok);
}

void prof_leave(prof_token_t tok, uint64_t tick) {
  MicroProfileLeave(tok, tick);
}

void prof_stat_register(struct prof_stat *stat) {
  stat->tok = prof_get_count_token(stat->name);

  list_add(&prof_stats, &stat->it);
}

void prof_stat_unregister(struct prof_stat *stat) {
  list_remove(&prof_stats, &stat->it);
}

void prof_count(prof_token_t tok, int count) {
  MicroProfileCounterSet(tok, count);
}

void prof_flip() {
  /* flip count-based stats every second */
  int64_t now = time_nanoseconds();
  int64_t next_update = last_update + NS_PER_SEC;

  if (now > next_update) {
    list_for_each_entry(stat, &prof_stats, struct prof_stat, it) {
      prof_count(stat->tok, *stat->n);
      *stat->n = 0;
    }

    last_update = now;
  }

  /* flip time-based stats every frame */
  MicroProfileFlip();
}

}
