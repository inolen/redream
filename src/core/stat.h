#ifndef STAT_H
#define STAT_H

#include "core/assert.h"
#include "core/constructor.h"
#include "core/list.h"
#include "core/profiler.h"
#include "core/rb_tree.h"

#define DECLARE_STAT(name)         \
  extern int64_t STAT_PREV_##name; \
  extern int64_t STAT_##name

#define DEFINE_STAT(group_name, name)                        \
  int64_t STAT_PREV_##name;                                  \
  int64_t STAT_##name;                                       \
  static struct stat STAT_STRUCT_##name = {                  \
      NULL, #name, &STAT_PREV_##name, &STAT_##name, 0, {0}}; \
  CONSTRUCTOR(STAT_REGISTER_##name) {                        \
    stat_register(group_name, &STAT_STRUCT_##name);          \
  }                                                          \
  DESTRUCTOR(STAT_UNREGISTER_##name) {                       \
    stat_unregister(group_name, &STAT_STRUCT_##name);        \
  }

#define STAT_UPDATE(group_name)            \
  {                                        \
    struct stat_group *group = NULL;       \
    if (!group) {                          \
      group = stat_find_group(group_name); \
      CHECK(group);                        \
    }                                      \
    stat_update(group);                    \
  }

struct stat_group {
  char name[128];
  struct list stats;
  int64_t last_update;
  struct rb_node it;
};

struct stat {
  struct stat_group *group;
  const char *name;
  int64_t *prev;
  int64_t *n;
  prof_token_t tok;
  struct list_node it;
};

struct stat_group *stat_find_group(const char *group_name);
void stat_register(const char *group, struct stat *stat);
void stat_unregister(const char *group, struct stat *stat);
void stat_update(struct stat_group *group);

#endif
