#include <stddef.h>
#include "core/interval_tree.h"
#include "core/core.h"

static int interval_tree_cmp(const struct rb_node *lhs,
                             const struct rb_node *rhs);
static void interval_tree_augment_propagate(struct rb_tree *t,
                                            struct rb_node *n);
static void interval_tree_augment_rotate(struct rb_tree *t,
                                         struct rb_node *oldn,
                                         struct rb_node *newn);

struct rb_callbacks interval_tree_cb = {
    &interval_tree_cmp, &interval_tree_augment_propagate,
    &interval_tree_augment_rotate,
};

static interval_type_t interval_node_max(struct interval_node *n) {
  return n ? n->max : 0;
}

static int interval_node_size(struct interval_node *n) {
  return n ? n->size : 0;
}

static int interval_node_height(struct interval_node *n) {
  return n ? n->height : 0;
}

static void interval_node_fix_counts(struct interval_node *n) {
  if (!n) {
    return;
  }

  struct interval_node *l = INTERVAL_NODE(n->rb.left);
  struct interval_node *r = INTERVAL_NODE(n->rb.right);

  n->size = 1 + interval_node_size(l) + interval_node_size(r);
  n->height = 1 + MAX(interval_node_height(l), interval_node_height(r));
  n->max = MAX(MAX(n->high, interval_node_max(l)), interval_node_max(r));
}

static void interval_tree_augment_propagate(struct rb_tree *t,
                                            struct rb_node *rb_n) {
  struct interval_node *n = rb_entry(rb_n, struct interval_node, rb);

  while (n) {
    interval_node_fix_counts(n);
    n = INTERVAL_NODE(n->rb.parent);
  }
}

static void interval_tree_augment_rotate(struct rb_tree *t,
                                         struct rb_node *rb_oldn,
                                         struct rb_node *rb_newn) {
  struct interval_node *oldn = rb_entry(rb_oldn, struct interval_node, rb);
  struct interval_node *newn = rb_entry(rb_newn, struct interval_node, rb);

  interval_node_fix_counts(oldn);
  interval_node_fix_counts(newn);
  interval_node_fix_counts(INTERVAL_NODE(newn->rb.parent));
}

static int interval_tree_cmp(const struct rb_node *rb_lhs,
                             const struct rb_node *rb_rhs) {
  struct interval_node *lhs = rb_entry(rb_lhs, struct interval_node, rb);
  struct interval_node *rhs = rb_entry(rb_rhs, struct interval_node, rb);

  if (lhs->low < rhs->low) {
    return -1;
  } else if (lhs->low > rhs->low) {
    return 1;
  } else if (lhs->high < rhs->high) {
    return -1;
  } else if (lhs->high > rhs->high) {
    return 1;
  } else {
    return 0;
  }
}

static int interval_tree_intersects(const struct interval_node *n,
                                    interval_type_t low, interval_type_t high) {
  return high >= n->low && n->high >= low;
}

struct interval_node *interval_tree_iter_next(struct interval_tree_it *it) {
  it->n = interval_tree_next_interval(it->n, it->low, it->high);
  return it->n;
}

struct interval_node *interval_tree_iter_first(struct rb_tree *t,
                                               interval_type_t low,
                                               interval_type_t high,
                                               struct interval_tree_it *it) {
  it->low = low;
  it->high = high;
  it->n = interval_tree_min_interval(INTERVAL_NODE(t->root), low, high);
  return it->n;
}

struct interval_node *interval_tree_find(struct rb_tree *t, interval_type_t low,
                                         interval_type_t high) {
  struct interval_node *n = INTERVAL_NODE(t->root);

  while (n) {
    struct interval_node *l = INTERVAL_NODE(n->rb.left);
    struct interval_node *r = INTERVAL_NODE(n->rb.right);

    if (interval_tree_intersects(n, low, high)) {
      return n;
    } else if (!l || l->max < low) {
      n = r;
    } else {
      n = l;
    }
  }

  return NULL;
}

struct interval_node *interval_tree_next_interval(struct interval_node *n,
                                                  interval_type_t low,
                                                  interval_type_t high) {
  while (n) {
    /* try to find the minimum node in the right subtree */
    if (n->rb.right) {
      struct interval_node *min =
          interval_tree_min_interval(INTERVAL_NODE(n->rb.right), low, high);
      if (min) {
        return min;
      }
    }

    /* else, move up the tree until a left child link is traversed */
    struct interval_node *c = n;
    n = INTERVAL_NODE(n->rb.parent);
    while (n && INTERVAL_NODE(n->rb.right) == c) {
      c = n;
      n = INTERVAL_NODE(n->rb.parent);
    }
    if (n && interval_tree_intersects(n, low, high)) {
      return n;
    }
  }

  return NULL;
}

struct interval_node *interval_tree_min_interval(struct interval_node *n,
                                                 interval_type_t low,
                                                 interval_type_t high) {
  struct interval_node *min = NULL;

  while (n) {
    int intersects = interval_tree_intersects(n, low, high);

    if (intersects) {
      min = n;
    }

    /* if n->left->max < low, there is no match in the left subtree, there
       could be one in the right */
    if (!n->rb.left || INTERVAL_NODE(n->rb.left)->max < low) {
      /* don't go right if the current node intersected */
      if (intersects) {
        break;
      }
      n = INTERVAL_NODE(n->rb.right);
    }
    /* else if n->left-max >= low, there could be one in the left subtree. if
       there isn't one in the left, there wouldn't be one in the right */
    else {
      n = INTERVAL_NODE(n->rb.left);
    }
  }

  return min;
}

int interval_tree_height(struct rb_tree *t) {
  struct interval_node *root = INTERVAL_NODE(t->root);
  return interval_node_height(root);
}

int interval_tree_size(struct rb_tree *t) {
  struct interval_node *root = INTERVAL_NODE(t->root);
  return interval_node_size(root);
}

interval_type_t interval_tree_max(struct rb_tree *t) {
  struct interval_node *root = INTERVAL_NODE(t->root);
  return interval_node_max(root);
}

void interval_tree_clear(struct rb_tree *t) {
  t->root = NULL;
}

void interval_tree_remove(struct rb_tree *t, struct interval_node *n) {
  rb_unlink(t, RB_NODE(n), &interval_tree_cb);
}

void interval_tree_insert(struct rb_tree *t, struct interval_node *n) {
  rb_insert(t, RB_NODE(n), &interval_tree_cb);
}
