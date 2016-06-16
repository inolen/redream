#ifndef PASS_STATS_H
#define PASS_STATS_H

#include "core/constructor.h"
#include "core/list.h"

#define DEFINE_STAT(name, desc)                                       \
  static int STAT_##name;                                             \
  static pass_stat_t STAT_T_##name = {#name, desc, &STAT_##name, {}}; \
  CONSTRUCTOR(STAT_REGISTER_##name) {                                 \
    pass_stat_register(&STAT_T_##name);                               \
  }                                                                   \
  DESTRUCTOR(STAT_UNREGISTER_##name) {                                \
    pass_stat_unregister(&STAT_T_##name);                             \
  }

typedef struct {
  const char *name;
  const char *desc;
  int *n;
  list_node_t it;
} pass_stat_t;

void pass_stat_register(pass_stat_t *stat);
void pass_stat_unregister(pass_stat_t *stat);
void pass_stat_print_all();

#endif
