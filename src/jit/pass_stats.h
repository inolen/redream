#ifndef PASS_STATS_H
#define PASS_STATS_H

#include "core/constructor.h"
#include "core/list.h"

#define DEFINE_PASS_STAT(name, desc)                                        \
  static int STAT_##name;                                                   \
  static struct pass_stat STAT_T_##name = {#name, desc, &STAT_##name, {0}}; \
  CONSTRUCTOR(STAT_REGISTER_##name) {                                       \
    pass_stats_register(&STAT_T_##name);                                    \
  }                                                                         \
  DESTRUCTOR(STAT_UNREGISTER_##name) {                                      \
    pass_stats_unregister(&STAT_T_##name);                                  \
  }

struct pass_stat {
  const char *name;
  const char *desc;
  int *n;
  struct list_node it;
};

void pass_stats_register(struct pass_stat *stat);
void pass_stats_unregister(struct pass_stat *stat);
void pass_stats_dump();

#endif
