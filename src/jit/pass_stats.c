#include "jit/pass_stats.h"
#include "core/core.h"

static struct list stats;

void pass_stats_register(struct pass_stat *stat) {
  list_add(&stats, &stat->it);
}

void pass_stats_unregister(struct pass_stat *stat) {
  list_remove(&stats, &stat->it);
}

void pass_stats_dump() {
  LOG_INFO("===-----------------------------------------------------===");
  LOG_INFO("pass stats");
  LOG_INFO("===-----------------------------------------------------===");

  int w = 0;
  list_for_each_entry(stat, &stats, struct pass_stat, it) {
    int l = (int)strlen(stat->desc);
    w = MAX(l, w);
  }

  list_for_each_entry(stat, &stats, struct pass_stat, it) {
    LOG_INFO("%-*s  %d", w, stat->desc, *stat->n);
  }

  LOG_INFO("");
}
