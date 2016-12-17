#include "jit/emit_stats.h"
#include "core/assert.h"
#include "core/math.h"
#include "core/string.h"

#define EMIT_STATS_MAX 512

struct emit_stat {
  char name[32];
  int n;
  int count;
};

static struct emit_stat stats[EMIT_STATS_MAX];
static int num_stats;

static struct emit_stat *emit_stats_demand(const char *name) {
  /* this is terribly slow, should really be using a hashtable,
     but this is currently only used when debugging with recc */
  for (int i = 0; i < num_stats; i++) {
    struct emit_stat *stat = &stats[i];
    if (!strcmp(stat->name, name)) {
      return stat;
    }
  }

  CHECK_LT(num_stats, EMIT_STATS_MAX);
  struct emit_stat *stat = &stats[num_stats++];
  strncpy(stat->name, name, sizeof(stat->name));
  return stat;
}

void emit_stats_add(const char *name, int count) {
  struct emit_stat *stat = emit_stats_demand(name);
  stat->count += count;
  stat->n++;
}

void emit_stats_dump() {
  LOG_INFO("===-----------------------------------------------------===");
  LOG_INFO("Emit stats");
  LOG_INFO("===-----------------------------------------------------===");

  int w = 5; /* TOTAL */
  int total_n = 0;
  int total_count = 0;
  float total_avg = 0.0f;

  for (int i = 0; i < num_stats; i++) {
    struct emit_stat *stat = &stats[i];
    int l = (int)strlen(stat->name);
    w = MAX(l, w);
    total_n += stat->n;
    total_count += stat->count;
  }

  if (total_n) {
    total_avg = total_count / (float)total_n;
  }

  for (int i = 0; i < num_stats; i++) {
    struct emit_stat *stat = &stats[i];
    float avg = stat->n ? stat->count / (float)stat->n : 0.0f;
    LOG_INFO("%*s, %9d, %9.2f", w, stat->name, stat->n, avg);
  }

  LOG_INFO("%*s, %9d, %9.2f", w, "TOTAL", total_n, total_avg);
  LOG_INFO("");
}
