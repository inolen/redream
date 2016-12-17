#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H

#include <stdint.h>
#include "core/rb_tree.h"

#define INTERVAL_NODE(n) (container_of_safe((n), struct interval_node, rb))

typedef uintptr_t interval_type_t;

struct interval_node {
  struct rb_node rb;
  interval_type_t low;
  interval_type_t high;
  interval_type_t max;
  int size;
  int height;
};

struct interval_tree_it {
  interval_type_t low;
  interval_type_t high;
  struct interval_node *n;
};

void interval_tree_insert(struct rb_tree *t, struct interval_node *n);
void interval_tree_remove(struct rb_tree *t, struct interval_node *n);
void interval_tree_clear(struct rb_tree *t);

interval_type_t interval_tree_max(struct rb_tree *t);
int interval_tree_size(struct rb_tree *t);
int interval_tree_height(struct rb_tree *t);

struct interval_node *interval_tree_min_interval(struct interval_node *n,
                                                 interval_type_t low,
                                                 interval_type_t high);
struct interval_node *interval_tree_next_interval(struct interval_node *n,
                                                  interval_type_t low,
                                                  interval_type_t high);

struct interval_node *interval_tree_find(struct rb_tree *t, interval_type_t low,
                                         interval_type_t high);

struct interval_node *interval_tree_iter_first(struct rb_tree *t,
                                               interval_type_t low,
                                               interval_type_t high,
                                               struct interval_tree_it *it);
struct interval_node *interval_tree_iter_next(struct interval_tree_it *it);

#endif
