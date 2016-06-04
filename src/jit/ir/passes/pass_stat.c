#include "core/assert.h"
#include "core/math.h"
#include "core/string.h"
#include "jit/ir/passes/pass_stat.h"

static list_t s_stats;

void pass_stat_register(pass_stat_t *stat) {
  list_add(&s_stats, &stat->it);
}

void pass_stat_unregister(pass_stat_t *stat) {
  list_remove(&s_stats, &stat->it);
}

void pass_stat_print_all() {
  LOG_INFO("===-----------------------------------------------------===");
  LOG_INFO("Pass stats");
  LOG_INFO("===-----------------------------------------------------===");

  int w = 0;
  list_for_each_entry(stat, &s_stats, pass_stat_t, it) {
    int l = (int)strlen(stat->desc);
    w = MAX(l, w);
  }

  list_for_each_entry(stat, &s_stats, pass_stat_t, it) {
    LOG_INFO("%-*s  %d", w, stat->desc, stat->n);
  }
}
