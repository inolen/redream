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

struct rb_callbacks {
  int (*cmp)(const struct rb_node *, const struct rb_node *);
  void (*propagate)(struct rb_tree *, struct rb_node *);
  void (*rotate)(struct rb_tree *, struct rb_node *, struct rb_node *);
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

#define rb_empty_tree(t) (!(t)->root)

#define rb_empty_node(n) (!(n)->parent && (n)->color != RB_BLACK)

#define rb_for_each(it, t) \
  for (struct rb_node *it = rb_first((t)); it; it = rb_next(it))

#define rb_entry(n, type, member) container_of_safe(n, type, member)

#define rb_first_entry(t, type, member) rb_entry(rb_first(t), type, member)

#define rb_last_entry(t, type, member) rb_entry(rb_last(t), type, member)

#define rb_next_entry(entry, type, member) \
  rb_entry(rb_next(&entry->member), type, member)

#define rb_prev_entry(entry, type, member) \
  rb_entry(rb_prev(&entry->member), type, member)

#define rb_find_entry(t, search, type, member, cb) \
  rb_entry(rb_find(t, &(search)->member, cb), type, member)

#define rb_for_each_entry(it, t, type, member)         \
  for (type *it = rb_first_entry(t, type, member); it; \
       it = rb_next_entry(it, type, member))

#define rb_for_each_entry_safe(it, t, type, member)                   \
  for (type *it = rb_first_entry(t, type, member),                    \
            *it##_next = it ? rb_next_entry(it, type, member) : NULL; \
       it; it = it##_next,                                            \
            it##_next = it ? rb_next_entry(it, type, member) : NULL)

#endif
