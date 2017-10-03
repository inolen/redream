#include "core/rb_tree.h"
#include "core/core.h"

#define VERIFY_TREE 0

static enum rb_color rb_color(struct rb_node *n) {
  return n ? n->color : RB_BLACK;
}

static struct rb_node *rb_grandparent(struct rb_node *n) {
  CHECK_NOTNULL(n->parent, "not the root node");
  CHECK_NOTNULL(n->parent->parent, "not child of root");
  return n->parent->parent;
}

static struct rb_node *rb_sibling(struct rb_node *n) {
  CHECK_NOTNULL(n->parent, "root node has no sibling");
  if (n == n->parent->left) {
    return n->parent->right;
  } else {
    return n->parent->left;
  }
}

static struct rb_node *rb_uncle(struct rb_node *n) {
  CHECK_NOTNULL(n->parent, "root node has no uncle");
  CHECK_NOTNULL(n->parent->parent, "children of root have no uncle");
  return rb_sibling(n->parent);
}

static struct rb_node *rb_min(struct rb_node *n) {
  while (n && n->left) {
    n = n->left;
  }
  return n;
}

static struct rb_node *rb_max(struct rb_node *n) {
  while (n && n->right) {
    n = n->right;
  }
  return n;
}

/* All paths from any given node to its leaf nodes contain the same number of
   black nodes. This one is the trickiest to verify; we do it by traversing
   the tree, incrementing a black node count as we go. The first time we reach
   a leaf we save the count. We return the count so that when we subsequently
   reach other leaves, we compare the count to the saved count. */
static int rb_verify_3(struct rb_node *n, int black_count,
                       int path_black_count) {
  if (rb_color(n) == RB_BLACK) {
    black_count++;
  }

  if (!n) {
    if (path_black_count == -1) {
      path_black_count = black_count;
    } else {
      CHECK_EQ(black_count, path_black_count);
    }
    return path_black_count;
  }

  path_black_count = rb_verify_3(n->left, black_count, path_black_count);
  path_black_count = rb_verify_3(n->right, black_count, path_black_count);

  return path_black_count;
}

/* Every red node has two children, and both are black (or equivalently, the
   parent of every red node is black). */
static void rb_verify_2(struct rb_node *n) {
  if (!n) {
    return;
  }

  if (rb_color(n) == RB_RED) {
    CHECK_EQ(rb_color(n->left), RB_BLACK);
    CHECK_EQ(rb_color(n->right), RB_BLACK);
    CHECK_EQ(rb_color(n->parent), RB_BLACK);
  }

  rb_verify_2(n->left);
  rb_verify_2(n->right);
}

static void rb_verify_1(struct rb_node *root) {
  CHECK_EQ(rb_color(root), RB_BLACK);
}

static void rb_verify(struct rb_node *n) {
  rb_verify_1(n);
  rb_verify_2(n);
  rb_verify_3(n, 0, -1);
}

static void rb_replace_node(struct rb_tree *t, struct rb_node *oldn,
                            struct rb_node *newn) {
  if (oldn->parent) {
    if (oldn == oldn->parent->left) {
      oldn->parent->left = newn;
    } else {
      oldn->parent->right = newn;
    }
  } else {
    t->root = newn;
  }

  if (newn) {
    newn->parent = oldn->parent;
  }
}

static void rb_swap_node(struct rb_tree *t, struct rb_node *a,
                         struct rb_node *b) {
  struct rb_node tmp = *a;

  /* note, swapping pointers is complicated by the case where a parent is
     being swapped with its child, for example:
       a  ->    b
     b        a
     in this case, swap(a, b) would result in a->parent == a, when it
     should be b */
  if ((a->parent = b->parent == a ? b : b->parent)) {
    if (a->parent->left == b) {
      a->parent->left = a;
    } else if (a->parent->right == b) {
      a->parent->right = a;
    }
  } else {
    t->root = a;
  }
  if ((a->left = b->left == a ? b : b->left)) {
    a->left->parent = a;
  }
  if ((a->right = b->right == a ? b : b->right)) {
    a->right->parent = a;
  }
  a->color = b->color;

  if ((b->parent = tmp.parent == b ? a : tmp.parent)) {
    if (b->parent->left == a) {
      b->parent->left = b;
    } else if (b->parent->right == a) {
      b->parent->right = b;
    }
  } else {
    t->root = b;
  }
  if ((b->left = tmp.left == b ? a : tmp.left)) {
    b->left->parent = b;
  }
  if ((b->right = tmp.right == b ? a : tmp.right)) {
    b->right->parent = b;
  }
  b->color = tmp.color;
}

/*  n          r
      r  ->  n
    l          l */
static void rb_rotate_left(struct rb_tree *t, struct rb_node *n,
                           struct rb_callbacks *cb) {
  struct rb_node *r = n->right;
  rb_replace_node(t, n, r);
  n->right = r->left;
  if (n->right) {
    n->right->parent = n;
  }
  r->left = n;
  r->left->parent = r;

  if (cb && cb->rotate) {
    cb->rotate(t, n, r);
  }
}

/*   n         l
   l      ->     n
     r         r */
static void rb_rotate_right(struct rb_tree *t, struct rb_node *n,
                            struct rb_callbacks *cb) {
  struct rb_node *l = n->left;
  struct rb_node *r = l->right;
  rb_replace_node(t, n, l);
  n->left = r;
  if (n->left) {
    n->left->parent = n;
  }
  l->right = n;
  l->right->parent = l;

  if (cb && cb->rotate) {
    cb->rotate(t, n, l);
  }
}

/* In this final case, we deal with two cases that are mirror images of one
   another:
   * The new node is the left child of its parent and the parent is the left
   child of the grandparent. In this case we rotate right about the
   grandparent.
   * The new node is the right child of its parent and the parent is the right
   child of the grandparent. In this case we rotate left about the
   grandparent.
   Now the properties are satisfied and all cases have been covered. */
static void rb_link_5(struct rb_tree *t, struct rb_node *n,
                      struct rb_callbacks *cb) {
  n->parent->color = RB_BLACK;
  rb_grandparent(n)->color = RB_RED;
  if (n == n->parent->left && n->parent == rb_grandparent(n)->left) {
    rb_rotate_right(t, rb_grandparent(n), cb);
  } else {
    CHECK(n == n->parent->right && n->parent == rb_grandparent(n)->right);
    rb_rotate_left(t, rb_grandparent(n), cb);
  }
}

/* In this case, we deal with two cases that are mirror images of one another:
   * The new node is the right child of its parent and the parent is the left
   child of the grandparent. In this case we rotate left about the parent.
   * The new node is the left child of its parent and the parent is the right
   child of the grandparent. In this case we rotate right about the parent.
   Neither of these fixes the properties, but they put the tree in the correct
   form to apply case 5. */
static void rb_link_4(struct rb_tree *t, struct rb_node *n,
                      struct rb_callbacks *cb) {
  if (n == n->parent->right && n->parent == rb_grandparent(n)->left) {
    rb_rotate_left(t, n->parent, cb);
    n = n->left;
  } else if (n == n->parent->left && n->parent == rb_grandparent(n)->right) {
    rb_rotate_right(t, n->parent, cb);
    n = n->right;
  }

  rb_link_5(t, n, cb);
}

/* In this case, the uncle node is red. We recolor the parent and uncle black
   and the grandparent red. However, the red grandparent node may now violate
   the red-black tree properties; we recursively invoke this procedure on it
   from case 1 to deal with this. */
static void rb_link_1(struct rb_tree *t, struct rb_node *n,
                      struct rb_callbacks *cb);

static void rb_link_3(struct rb_tree *t, struct rb_node *n,
                      struct rb_callbacks *cb) {
  if (rb_color(rb_uncle(n)) == RB_RED) {
    n->parent->color = RB_BLACK;
    rb_uncle(n)->color = RB_BLACK;
    rb_grandparent(n)->color = RB_RED;
    rb_link_1(t, rb_grandparent(n), cb);
    return;
  }

  rb_link_4(t, n, cb);
}

/* In this case, the new node has a black parent. All the properties are still
   satisfied and we return. */
static void rb_link_2(struct rb_tree *t, struct rb_node *n,
                      struct rb_callbacks *cb) {
  if (rb_color(n->parent) == RB_BLACK) {
    /* tree is still valid */
    return;
  }

  rb_link_3(t, n, cb);
}

/* In this case, the new node is now the root node of the tree. Since the root
   node must be black, and changing its color adds the same number of black
   nodes to every path, we simply recolor it black. Because only the root node
   has no parent, we can assume henceforth that the node has a parent. */
static void rb_link_1(struct rb_tree *t, struct rb_node *n,
                      struct rb_callbacks *cb) {
  if (!n->parent) {
    return;
  }

  rb_link_2(t, n, cb);
}

/* There are two cases handled here which are mirror images of one another:
   * N's sibling S is black, S's right child is red, and N is the left child
   of its parent. We exchange the colors of N's parent and sibling, make S's
   right child black, then rotate left at N's parent.
   * N's sibling S is black, S's left child is red, and N is the right child
   of its parent. We exchange the colors of N's parent and sibling, make S's
   left child black, then rotate right at N's parent.

   This accomplishes three things at once:
   * We add a black node to all paths through N, either by adding a black S to
   those paths or by recoloring N's parent black.
   * We remove a black node from all paths through S's red child, either by
   removing P from those paths or by recoloring S.
   * We recolor S's red child black, adding a black node back to all paths
   through S's red child.

   S's left child has become a child of N's parent during the rotation and so
   is unaffected. */
static void rb_unlink_6(struct rb_tree *t, struct rb_node *n,
                        struct rb_callbacks *cb) {
  rb_sibling(n)->color = rb_color(n->parent);
  n->parent->color = RB_BLACK;
  if (n == n->parent->left) {
    CHECK_EQ(rb_color(rb_sibling(n)->right), RB_RED);
    rb_sibling(n)->right->color = RB_BLACK;
    rb_rotate_left(t, n->parent, cb);
  } else {
    CHECK_EQ(rb_color(rb_sibling(n)->left), RB_RED);
    rb_sibling(n)->left->color = RB_BLACK;
    rb_rotate_right(t, n->parent, cb);
  }
}

/* There are two cases handled here which are mirror images of one another:
   * N's sibling S is black, S's left child is red, S's right child is black,
   and N is the left child of its parent. We exchange the colors of S and its
   left sibling and rotate right at S.
   * N's sibling S is black, S's right child is red, S's left child is black,
   and N is the right child of its parent. We exchange the colors of S and its
   right sibling and rotate left at S.
   Both of these function to reduce us to the situation described in case 6. */
static void rb_unlink_5(struct rb_tree *t, struct rb_node *n,
                        struct rb_callbacks *cb) {
  if (n == n->parent->left && rb_color(rb_sibling(n)) == RB_BLACK &&
      rb_color(rb_sibling(n)->left) == RB_RED &&
      rb_color(rb_sibling(n)->right) == RB_BLACK) {
    rb_sibling(n)->color = RB_RED;
    rb_sibling(n)->left->color = RB_BLACK;
    rb_rotate_right(t, rb_sibling(n), cb);
  } else if (n == n->parent->right && rb_color(rb_sibling(n)) == RB_BLACK &&
             rb_color(rb_sibling(n)->right) == RB_RED &&
             rb_color(rb_sibling(n)->left) == RB_BLACK) {
    rb_sibling(n)->color = RB_RED;
    rb_sibling(n)->right->color = RB_BLACK;
    rb_rotate_left(t, rb_sibling(n), cb);
  }

  rb_unlink_6(t, n, cb);
}

/* N's sibling and sibling's children are black, but its parent is red. We
   exchange the colors of the sibling and parent; this restores the tree
   properties. */
static void rb_unlink_4(struct rb_tree *t, struct rb_node *n,
                        struct rb_callbacks *cb) {
  if (rb_color(n->parent) == RB_RED && rb_color(rb_sibling(n)) == RB_BLACK &&
      rb_color(rb_sibling(n)->left) == RB_BLACK &&
      rb_color(rb_sibling(n)->right) == RB_BLACK) {
    rb_sibling(n)->color = RB_RED;
    n->parent->color = RB_BLACK;
    return;
  }

  rb_unlink_5(t, n, cb);
}

/* In this case N's parent, sibling, and sibling's children are black. In this
   case we paint the sibling red. Now all paths passing through N's parent
   have one less black node than before the deletion, so we must recursively
   run this procedure from case 1 on N's parent. */
static void rb_unlink_1(struct rb_tree *t, struct rb_node *n,
                        struct rb_callbacks *cb);

static void rb_unlink_3(struct rb_tree *t, struct rb_node *n,
                        struct rb_callbacks *cb) {
  if (rb_color(n->parent) == RB_BLACK && rb_color(rb_sibling(n)) == RB_BLACK &&
      rb_color(rb_sibling(n)->left) == RB_BLACK &&
      rb_color(rb_sibling(n)->right) == RB_BLACK) {
    rb_sibling(n)->color = RB_RED;
    rb_unlink_1(t, n->parent, cb);
    return;
  }

  rb_unlink_4(t, n, cb);
}

/* N has a red sibling. In this case we exchange the colors of the parent and
   sibling, then rotate about the parent so that the sibling becomes the
   parent of its former parent. This does not restore the tree properties, but
   reduces the problem to one of the remaining cases. */
static void rb_unlink_2(struct rb_tree *t, struct rb_node *n,
                        struct rb_callbacks *cb) {
  if (rb_color(rb_sibling(n)) == RB_RED) {
    n->parent->color = RB_RED;
    rb_sibling(n)->color = RB_BLACK;
    if (n == n->parent->left) {
      rb_rotate_left(t, n->parent, cb);
    } else {
      rb_rotate_right(t, n->parent, cb);
    }
  }

  rb_unlink_3(t, n, cb);
}

/* In this case, N has become the root node. The deletion removed one black
   node from every path, so no properties are violated. */
static void rb_unlink_1(struct rb_tree *t, struct rb_node *n,
                        struct rb_callbacks *cb) {
  if (!n->parent) {
    return;
  }

  rb_unlink_2(t, n, cb);
}

void rb_link(struct rb_tree *t, struct rb_node *n, struct rb_callbacks *cb) {
  /* reset node state other than the parent the node is to be linked to */
  n->left = NULL;
  n->right = NULL;
  n->color = RB_RED;

  /* set initial root */
  if (!t->root) {
    t->root = n;
  }

  /* adjust tree, starting at the newly inserted node, to satisfy the
     properties of a valid red-black tree */
  rb_link_1(t, n, cb);

  /* force root to black */
  t->root->color = RB_BLACK;

  /* fix up each node in the chain */
  if (cb && cb->propagate) {
    cb->propagate(t, n);
  }

#if VERIFY_TREE
  rb_verify(t->root);
#endif
}

void rb_unlink(struct rb_tree *t, struct rb_node *n, struct rb_callbacks *cb) {
  CHECK(!rb_empty_node(n));

  /* when deleting a node with two non-leaf children, we swap the node with
     its in-order predecessor (the maximum or rightmost element in the left
     subtree), and then delete the original node which now has only one
     non-leaf child */
  if (n->left && n->right) {
    struct rb_node *pred = rb_max(n->left);
    rb_swap_node(t, n, pred);
  }

  /* a node with at most one non-leaf child can simply be replaced with its
     non-leaf child */
  CHECK(!n->left || !n->right);
  struct rb_node *child = n->right ? n->right : n->left;
  if (rb_color(n) == RB_BLACK) {
    rb_unlink_1(t, n, cb);
  }
  rb_replace_node(t, n, child);

  /* force root to black */
  if (t->root) {
    t->root->color = RB_BLACK;
  }

  /* fix up each node in the parent chain */
  if (cb && cb->propagate) {
    cb->propagate(t, n->parent);
  }

  /* clear node state to support rb_empty_node */
  memset(n, 0, sizeof(*n));

#if VERIFY_TREE
  rb_verify(t->root);
#endif
}

void rb_insert(struct rb_tree *t, struct rb_node *n, struct rb_callbacks *cb) {
  /* insert node into the correct location in the tree, then link it in to
     recolor the tree */
  struct rb_node *parent = t->root;

  while (parent) {
    if (cb->cmp(n, parent) < 0) {
      if (!parent->left) {
        parent->left = n;
        break;
      }

      parent = parent->left;
    } else {
      if (!parent->right) {
        parent->right = n;
        break;
      }

      parent = parent->right;
    }
  }

  n->parent = parent;

  rb_link(t, n, cb);
}

struct rb_node *rb_find(struct rb_tree *t, const struct rb_node *search,
                        struct rb_callbacks *cb) {
  struct rb_node *n = t->root;

  while (n) {
    int cmp = cb->cmp(search, n);

    if (cmp == 0) {
      return n;
    } else if (cmp < 0) {
      n = n->left;
    } else {
      n = n->right;
    }
  }

  return NULL;
}

struct rb_node *rb_upper_bound(struct rb_tree *t, const struct rb_node *search,
                               struct rb_callbacks *cb) {
  struct rb_node *ub = NULL;
  struct rb_node *n = t->root;

  while (n) {
    int cmp = cb->cmp(search, n);

    if (cmp < 0) {
      ub = n;
      n = n->left;
    } else {
      n = n->right;
    }
  }

  return ub;
}

struct rb_node *rb_first(struct rb_tree *t) {
  return rb_min(t->root);
}

struct rb_node *rb_last(struct rb_tree *t) {
  return rb_max(t->root);
}

struct rb_node *rb_prev(struct rb_node *n) {
  if (!n) {
    return NULL;
  }

  if (n->left) {
    /* prev element is the largest element in the left subtree */
    n = rb_max(n->left);
  } else {
    /* prev element is the next smallest element upwards. walk up
       until we go left */
    struct rb_node *last = n;

    n = n->parent;

    while (n && n->left == last) {
      last = n;
      n = n->parent;
    }
  }

  return n;
}

struct rb_node *rb_next(struct rb_node *n) {
  if (!n) {
    return NULL;
  }

  if (n->right) {
    /* next element is the the smallest element in the right subtree */
    n = rb_min(n->right);
  } else {
    /* next element is the next largest element upwards. walk up until
       we go right */
    struct rb_node *last = n;

    n = n->parent;

    while (n && n->right == last) {
      last = n;
      n = n->parent;
    }
  }

  return n;
}
