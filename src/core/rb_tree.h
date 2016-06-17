#ifndef RB_TREE_H
#define RB_TREE_H

#include "core/core.h"

#define RB_NODE(n) ((struct rb_node *)n)

enum rb_color {
  RB_RED,
  RB_BLACK,
};

struct rb_node {
  struct rb_node *parent;
  struct rb_node *left;
  struct rb_node *right;
  enum rb_color color;
};

struct rb_tree {
  struct rb_node *root;
};

typedef int (*rb_cmp_cb)(const struct rb_node *, const struct rb_node *);
typedef void (*rb_augment_propagate_cb)(struct rb_tree *, struct rb_node *);
typedef void (*rb_augment_rotate_cb)(struct rb_tree *, struct rb_node *,
                                     struct rb_node *);

struct rb_callbacks {
  rb_cmp_cb cmp;
  rb_augment_propagate_cb propagate;
  rb_augment_rotate_cb rotate;
};

void rb_link(struct rb_tree *t, struct rb_node *n, struct rb_callbacks *cb);
void rb_unlink(struct rb_tree *t, struct rb_node *n, struct rb_callbacks *cb);
void rb_insert(struct rb_tree *t, struct rb_node *n, struct rb_callbacks *cb);

struct rb_node *rb_find(struct rb_tree *t, const struct rb_node *search,
                        struct rb_callbacks *cb);
struct rb_node *rb_upper_bound(struct rb_tree *t, const struct rb_node *search,
                               struct rb_callbacks *cb);

struct rb_node *rb_first(struct rb_tree *t);
struct rb_node *rb_last(struct rb_tree *t);
struct rb_node *rb_prev(struct rb_node *n);
struct rb_node *rb_next(struct rb_node *n);

#define rb_entry(n, type, member) container_of(n, type, member)

#define rb_first_entry(t, type, member)     \
  ({                                        \
    struct rb_node *n = rb_first(t);        \
    (n ? rb_entry(n, type, member) : NULL); \
  })

#define rb_last_entry(t, type, member)   \
  ({                                     \
    struct rb_node *n = rb_last(t);      \
    n ? rb_entry(n, type, member) : NULL \
  })

#define rb_next_entry(entry, member)                    \
  ({                                                    \
    struct rb_node *n = rb_next(&entry->member);        \
    (n ? rb_entry(n, TYPEOF(*(entry)), member) : NULL); \
  })

#define rb_prev_entry(entry, member)                    \
  ({                                                    \
    struct rb_node *n = rb_prev(&entry->member);        \
    (n ? rb_entry(n, TYPEOF(*(entry)), member) : NULL); \
  })

#define rb_find_entry(t, search, member, cb)                \
  ({                                                        \
    struct rb_node *it = rb_find(t, &(search)->member, cb); \
    (it ? rb_entry(it, TYPEOF(*search), member) : NULL);    \
  })

#define rb_for_each_entry(it, t, type, member)         \
  for (type *it = rb_first_entry(t, type, member); it; \
       it = rb_next_entry(it, member))

#endif
