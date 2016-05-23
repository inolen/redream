#include <stddef.h>
#include "core/interval_tree.h"
#include "core/math.h"

static int interval_tree_cmp(const interval_node_t *lhs,
                             const interval_node_t *rhs);
static void interval_tree_augment_propagate(rb_tree_t *t, interval_node_t *n);
static void interval_tree_augment_rotate(rb_tree_t *t, interval_node_t *oldn,
                                         interval_node_t *newn);

rb_callback_t interval_tree_cb = {
    (rb_cmp_cb)&interval_tree_cmp,
    (rb_augment_propagate_cb)&interval_tree_augment_propagate,
    (rb_augment_rotate_cb)&interval_tree_augment_rotate,
};

static interval_type_t interval_tree_node_max(interval_node_t *n) {
  return n ? n->max : 0;
}

static int interval_tree_node_size(interval_node_t *n) {
  return n ? n->size : 0;
}

static int interval_tree_node_height(interval_node_t *n) {
  return n ? n->height : 0;
}

static void interval_tree_fix_counts(interval_node_t *n) {
  if (!n) {
    return;
  }

  interval_node_t *l = INTERVAL_NODE(n->base.left);
  interval_node_t *r = INTERVAL_NODE(n->base.right);

  n->size = 1 + interval_tree_node_size(l) + interval_tree_node_size(r);
  n->height =
      1 + MAX(interval_tree_node_height(l), interval_tree_node_height(r));
  n->max =
      MAX(MAX(n->high, interval_tree_node_max(l)), interval_tree_node_max(r));
}

static void interval_tree_augment_propagate(rb_tree_t *t, interval_node_t *n) {
  while (n) {
    interval_tree_fix_counts(n);
    n = INTERVAL_NODE(n->base.parent);
  }
}

static void interval_tree_augment_rotate(rb_tree_t *t, interval_node_t *oldn,
                                         interval_node_t *newn) {
  interval_tree_fix_counts(oldn);
  interval_tree_fix_counts(newn);
  interval_tree_fix_counts(INTERVAL_NODE(newn->base.parent));
}

static int interval_tree_intersects(const interval_node_t *n,
                                    interval_type_t low, interval_type_t high) {
  return high >= n->low && n->high >= low;
}

static int interval_tree_cmp(const interval_node_t *lhs,
                             const interval_node_t *rhs) {
  int cmp = lhs->low - rhs->low;

  if (!cmp) {
    cmp = lhs->high - rhs->high;
  }

  return cmp;
}

static interval_node_t *interval_tree_min_interval(interval_node_t *n,
                                                   interval_type_t low,
                                                   interval_type_t high) {
  interval_node_t *min = NULL;

  while (n) {
    int intersects = interval_tree_intersects(n, low, high);

    if (intersects) {
      min = n;
    }

    // if n->left->max < low, there is no match in the left subtree, there
    // could be one in the right
    if (!n->base.left || INTERVAL_NODE(n->base.left)->max < low) {
      // don't go right if the current node intersected
      if (intersects) {
        break;
      }
      n = INTERVAL_NODE(n->base.right);
    }
    // else if n->left-max >= low, there could be one in the left subtree. if
    // there isn't one in the left, there wouldn't be one in the right
    else {
      n = INTERVAL_NODE(n->base.left);
    }
  }

  return min;
}

static interval_node_t *interval_tree_next_interval(interval_node_t *n,
                                                    interval_type_t low,
                                                    interval_type_t high) {
  while (n) {
    // try to find the minimum node in the right subtree
    if (n->base.right) {
      interval_node_t *min =
          interval_tree_min_interval(INTERVAL_NODE(n->base.right), low, high);
      if (min) {
        return min;
      }
    }

    // else, move up the tree until a left child link is traversed
    interval_node_t *c = n;
    n = INTERVAL_NODE(n->base.parent);
    while (n && INTERVAL_NODE(n->base.right) == c) {
      c = n;
      n = INTERVAL_NODE(n->base.parent);
    }
    if (n && interval_tree_intersects(n, low, high)) {
      return n;
    }
  }

  return NULL;
}

void interval_tree_insert(rb_tree_t *t, interval_node_t *n) {
  rb_insert(t, RB_NODE(n), &interval_tree_cb);
}

void interval_tree_remove(rb_tree_t *t, interval_node_t *n) {
  rb_unlink(t, RB_NODE(n), &interval_tree_cb);
}

void interval_tree_clear(rb_tree_t *t) {
  t->root = NULL;
}

interval_node_t *interval_tree_find(rb_tree_t *t, interval_type_t low,
                                    interval_type_t high) {
  interval_node_t *n = INTERVAL_NODE(t->root);

  while (n) {
    interval_node_t *l = INTERVAL_NODE(n->base.left);
    interval_node_t *r = INTERVAL_NODE(n->base.right);

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

interval_node_t *interval_tree_iter_first(rb_tree_t *t, interval_type_t low,
                                          interval_type_t high,
                                          interval_tree_iter_t *it) {
  it->low = low;
  it->high = high;
  it->n = interval_tree_min_interval(INTERVAL_NODE(t->root), low, high);
  return it->n;
}

interval_node_t *interval_tree_iter_next(interval_tree_iter_t *it) {
  it->n = interval_tree_next_interval(it->n, it->low, it->high);
  return it->n;
}
