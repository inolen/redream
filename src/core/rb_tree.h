#ifndef RB_TREE_H
#define RB_TREE_H

#include "core/core.h"

#define RB_NODE(n) ((rb_node_t *)n)

typedef enum {
  RB_RED,
  RB_BLACK,
} rb_color_t;

typedef struct rbnode_s {
  struct rbnode_s *parent;
  struct rbnode_s *left;
  struct rbnode_s *right;
  rb_color_t color;
} rb_node_t;

typedef struct { rb_node_t *root; } rb_tree_t;

typedef int (*rb_cmp_cb)(const rb_node_t *, const rb_node_t *);
typedef void (*rb_augment_propagate_cb)(rb_tree_t *, rb_node_t *);
typedef void (*rb_augment_rotate_cb)(rb_tree_t *, rb_node_t *, rb_node_t *);

typedef struct {
  rb_cmp_cb cmp;
  rb_augment_propagate_cb propagate;
  rb_augment_rotate_cb rotate;
} rb_callback_t;

void rb_link(rb_tree_t *t, rb_node_t *n, rb_callback_t *cb);
void rb_unlink(rb_tree_t *t, rb_node_t *n, rb_callback_t *cb);
void rb_insert(rb_tree_t *t, rb_node_t *n, rb_callback_t *cb);

rb_node_t *rb_find(rb_tree_t *t, const rb_node_t *search, rb_callback_t *cb);
rb_node_t *rb_upper_bound(rb_tree_t *t, const rb_node_t *search,
                          rb_callback_t *cb);

rb_node_t *rb_first(rb_tree_t *t);
rb_node_t *rb_last(rb_tree_t *t);
rb_node_t *rb_prev(rb_node_t *n);
rb_node_t *rb_next(rb_node_t *n);

#define rb_entry(n, type, member) container_of(n, type, member)

#define rb_first_entry(t, type, member)     \
  ({                                        \
    rb_node_t *n = rb_first(t);             \
    (n ? rb_entry(n, type, member) : NULL); \
  })

#define rb_last_entry(t, type, member)   \
  ({                                     \
    rb_node_t *n = rb_last(t);           \
    n ? rb_entry(n, type, member) : NULL \
  })

#define rb_next_entry(entry, member)                    \
  ({                                                    \
    rb_node_t *n = rb_next(&entry->member);             \
    (n ? rb_entry(n, TYPEOF(*(entry)), member) : NULL); \
  })

#define rb_prev_entry(entry, member)                    \
  ({                                                    \
    rb_node_t *n = rb_prev(&entry->member);             \
    (n ? rb_entry(n, TYPEOF(*(entry)), member) : NULL); \
  })

#define rb_find_entry(t, search, member, cb)             \
  ({                                                     \
    rb_node_t *it = rb_find(t, &(search)->member, cb);   \
    (it ? rb_entry(it, TYPEOF(*search), member) : NULL); \
  })

#define rb_for_each_entry(it, t, type, member)         \
  for (type *it = rb_first_entry(t, type, member); it; \
       it = rb_next_entry(it, member))

#endif
