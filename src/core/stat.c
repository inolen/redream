#include "core/stat.h"
#include "sys/time.h"

static struct rb_tree groups;

static int stat_group_cmp(const struct rb_node *rb_lhs,
                          const struct rb_node *rb_rhs) {
  const struct stat_group *lhs = rb_entry(rb_lhs, const struct stat_group, it);
  const struct stat_group *rhs = rb_entry(rb_rhs, const struct stat_group, it);
  return strcmp(lhs->name, rhs->name);
}

static struct rb_callbacks stat_group_cb = {&stat_group_cmp, NULL, NULL};

static struct stat_group *stat_alloc_group(const char *name) {
  struct stat_group *group = calloc(1, sizeof(struct stat_group));
  strncpy(group->name, name, sizeof(group->name));
  rb_insert(&groups, &group->it, &stat_group_cb);
  return group;
}

static void stat_free_group(struct stat_group *group) {
  rb_unlink(&groups, &group->it, &stat_group_cb);
  free(group);
}

struct stat_group *stat_find_group(const char *group_name) {
  struct stat_group search;
  strncpy(search.name, group_name, sizeof(search.name));
  return rb_find_entry(&groups, &search, struct stat_group, it, &stat_group_cb);
}

void stat_register(const char *group_name, struct stat *stat) {
  struct stat_group *group = stat_find_group(group_name);
  if (!group) {
    group = stat_alloc_group(group_name);
  }

  stat->group = group;
  stat->tok = prof_get_count_token(stat->name);
  list_add(&stat->group->stats, &stat->it);
}

void stat_unregister(const char *group_name, struct stat *stat) {
  list_remove(&stat->group->stats, &stat->it);

  if (list_empty(&stat->group->stats)) {
    stat_free_group(stat->group);
  }
}

void stat_update(struct stat_group *group) {
  int64_t now = time_nanoseconds();
  int64_t next_update = group->last_update + NS_PER_SEC;

  if (now > next_update) {
    list_for_each_entry(stat, &group->stats, struct stat, it) {
      prof_count(stat->tok, *stat->n);
      *stat->prev = *stat->n;
      *stat->n = 0;
    }

    group->last_update = now;
  }
}
