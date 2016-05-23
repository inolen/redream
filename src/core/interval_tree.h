#ifndef INTERVAL_TREE_C_H
#define INTERVAL_TREE_C_H

#include <stdint.h>
#include "core/rb_tree.h"

#define INTERVAL_NODE(n) ((interval_node_t *)n)

typedef uintptr_t interval_type_t;

typedef struct {
  rb_node_t base;
  interval_type_t low;
  interval_type_t high;
  interval_type_t max;
  int size;
  int height;
} interval_node_t;

typedef struct {
  interval_type_t low;
  interval_type_t high;
  interval_node_t *n;
} interval_tree_iter_t;

void interval_tree_insert(rb_tree_t *t, interval_node_t *n);
void interval_tree_remove(rb_tree_t *t, interval_node_t *n);
void interval_tree_clear(rb_tree_t *t);
interval_node_t *interval_tree_find(rb_tree_t *t, interval_type_t low,
                                    interval_type_t high);

interval_node_t *interval_tree_iter_first(rb_tree_t *t, interval_type_t low,
                                          interval_type_t high,
                                          interval_tree_iter_t *it);
interval_node_t *interval_tree_iter_next(interval_tree_iter_t *it);

#endif
