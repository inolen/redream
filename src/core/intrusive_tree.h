#ifndef INTRUSIVE_TREE_H
#define INTRUSIVE_TREE_H

#include <functional>
#include <stdlib.h>
#include "core/assert.h"

namespace re {

enum Color { RED = true, BLACK = false };

template <typename T>
struct IntrusiveTreeNode {
  IntrusiveTreeNode()
      : parent(nullptr), left(nullptr), right(nullptr), color(RED) {}

  T *grandparent() {
    CHECK_NOTNULL(parent);          // not the root node
    CHECK_NOTNULL(parent->parent);  // not child of root
    return parent->parent;
  }

  T *sibling() {
    CHECK_NOTNULL(parent);  // root node has no sibling
    if (this == parent->left) {
      return parent->right;
    } else {
      return parent->left;
    }
  }

  T *uncle() {
    CHECK_NOTNULL(parent);          // root node has no uncle
    CHECK_NOTNULL(parent->parent);  // children of root have no uncle
    return parent->sibling();
  }

  T *parent, *left, *right;
  Color color;
};

template <typename DerivedTree, typename T>
class IntrusiveTree {
 protected:
  IntrusiveTree() : root_(nullptr) {}

  T *Link(T *n) {
    // set initial root
    if (!root_) {
      root_ = n;
    }

    // adjust tree, starting at the newly inserted node, to satisfy the
    // properties of a valid red-black tree
    LinkCase1(n);

    // force root to black
    root_->color = BLACK;

    derived().AugmentPropagate(n->parent);

#ifdef VERIFY_INTRUSIVE_TREE
    VerifyProperties();
#endif

    return n;
  }

  void Unlink(T *n) {
    // when deleting a node with two non-leaf children, we swap the node with
    // its in-order predecessor (the maximum or rightmost element in the left
    // subtree), and then delete the original node which now has only one
    // non-leaf child
    if (n->left && n->right) {
      T *pred = MaxNode(n->left);
      SwapNode(n, pred);
    }

    // a node with at most one non-leaf child can simply be replaced with its
    // non-leaf child
    CHECK(!n->left || !n->right);
    T *child = n->right ? n->right : n->left;
    if (color(n) == BLACK) {
      UnlinkCase1(n);
    }
    ReplaceNode(n, child);

    // force root to black
    if (root_) {
      root_->color = BLACK;
    }

    // fix up each node in the parent chain
    derived().AugmentPropagate(n->parent);

#ifdef VERIFY_INTRUSIVE_TREE
    VerifyProperties();
#endif
  }

  T *root_;

 private:
  static inline Color color(T *n) { return n ? n->color : BLACK; }

  static void VerifyProperty1(T *root) { CHECK_EQ(color(root), BLACK); }

  // Every red node has two children, and both are black (or equivalently, the
  // parent of every red node is black).
  static void VerifyProperty2(T *n) {
    if (!n) {
      return;
    }
    if (color(n) == RED) {
      CHECK_EQ(color(n->left), BLACK);
      CHECK_EQ(color(n->right), BLACK);
      CHECK_EQ(color(n->parent), BLACK);
    }
    VerifyProperty2(n->left);
    VerifyProperty2(n->right);
  }

  // All paths from any given node to its leaf nodes contain the same number of
  // black nodes. This one is the trickiest to verify; we do it by traversing
  // the tree, incrementing a black node count as we go. The first time we reach
  // a leaf we save the count. We return the count so that when we subsequently
  // reach other leaves, we compare the count to the saved count.
  static int VerifyProperty3(T *n, int black_count = 0,
                             int path_black_count = -1) {
    if (color(n) == BLACK) {
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

    path_black_count = VerifyProperty3(n->left, black_count, path_black_count);
    path_black_count = VerifyProperty3(n->right, black_count, path_black_count);

    return path_black_count;
  }

  DerivedTree &derived() { return *static_cast<DerivedTree *>(this); }

  void VerifyProperties() {
    VerifyProperty1(root_);
    VerifyProperty2(root_);
    VerifyProperty3(root_);
  }

  //  n          r
  //    r  ->  n
  //  l          l
  void RotateLeft(T *n) {
    T *r = n->right;
    ReplaceNode(n, r);
    n->right = r->left;
    if (n->right) {
      n->right->parent = n;
    }
    r->left = n;
    r->left->parent = r;

    derived().AugmentRotate(n, r);
  }

  //   n         l
  // l      ->     n
  //   r         r
  void RotateRight(T *n) {
    T *l = n->left;
    T *r = l->right;
    ReplaceNode(n, l);
    n->left = r;
    if (n->left) {
      n->left->parent = n;
    }
    l->right = n;
    l->right->parent = l;

    derived().AugmentRotate(n, l);
  }

  void ReplaceNode(T *oldn, T *newn) {
    if (oldn->parent) {
      if (oldn == oldn->parent->left) {
        oldn->parent->left = newn;
      } else {
        oldn->parent->right = newn;
      }
    } else {
      root_ = newn;
    }

    if (newn) {
      newn->parent = oldn->parent;
    }
  }

  void SwapNode(T *a, T *b) {
    T tmp = *a;

    // note, swapping pointers is complicated by the case where a parent is
    // being swapped with its child, for example:
    //   a  ->    b
    // b        a
    // in this case, a swap(a, b) would result in a->parent == a, when it
    // should be b
    if ((a->parent = b->parent == a ? b : b->parent)) {
      if (a->parent->left == b) {
        a->parent->left = a;
      } else if (a->parent->right == b) {
        a->parent->right = a;
      }
    } else {
      root_ = a;
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
      root_ = b;
    }
    if ((b->left = tmp.left == b ? a : tmp.left)) {
      b->left->parent = b;
    }
    if ((b->right = tmp.right == b ? a : tmp.right)) {
      b->right->parent = b;
    }
    b->color = tmp.color;
  }

  // T *MinNode(T *n) {
  //   while (n && n->left) {
  //     n = n->left;
  //   }
  //   return n;
  // }

  T *MaxNode(T *n) {
    while (n && n->right) {
      n = n->right;
    }
    return n;
  }

  // T *NextNode(T *n) {
  //   if (n->right) {
  //     return MinNode(n->right);
  //   }

  //   T *p = n->parent;
  //   while (p && p->right == n) {
  //     n = p;
  //     p = p->parent;
  //   }
  //   return p;
  // }

  // In this case, the new node is now the root node of the tree. Since the root
  // node must be black, and changing its color adds the same number of black
  // nodes to every path, we simply recolor it black. Because only the root node
  // has no parent, we can assume henceforth that the node has a parent.
  void LinkCase1(T *n) {
    if (!n->parent) {
      return;
    }

    LinkCase2(n);
  }

  // In this case, the new node has a black parent. All the properties are still
  // satisfied and we return->
  void LinkCase2(T *n) {
    if (color(n->parent) == BLACK) {
      return;  // Tree is still valid
    }

    LinkCase3(n);
  }

  // In this case, the uncle node is red. We recolor the parent and uncle black
  // and the grandparent red. However, the red grandparent node may now violate
  // the red-black tree properties; we recursively invoke this procedure on it
  // from case 1 to deal with this.
  void LinkCase3(T *n) {
    if (color(n->uncle()) == RED) {
      n->parent->color = BLACK;
      n->uncle()->color = BLACK;
      n->grandparent()->color = RED;
      LinkCase1(n->grandparent());
      return;
    }

    LinkCase4(n);
  }

  // In this case, we deal with two cases that are mirror images of one another:
  // * The new node is the right child of its parent and the parent is the left
  // child of the grandparent. In this case we rotate left about the parent.
  // * The new node is the left child of its parent and the parent is the right
  // child of the grandparent. In this case we rotate right about the parent.
  // Neither of these fixes the properties, but they put the tree in the correct
  // form to apply case 5.
  void LinkCase4(T *n) {
    if (n == n->parent->right && n->parent == n->grandparent()->left) {
      RotateLeft(n->parent);
      n = n->left;
    } else if (n == n->parent->left && n->parent == n->grandparent()->right) {
      RotateRight(n->parent);
      n = n->right;
    }

    LinkCase5(n);
  }

  // In this final case, we deal with two cases that are mirror images of one
  // another:
  // * The new node is the left child of its parent and the parent is the left
  // child of the grandparent. In this case we rotate right about the
  // grandparent.
  // * The new node is the right child of its parent and the parent is the right
  // child of the grandparent. In this case we rotate left about the
  // grandparent.
  // Now the properties are satisfied and all cases have been covered.
  void LinkCase5(T *n) {
    n->parent->color = BLACK;
    n->grandparent()->color = RED;
    if (n == n->parent->left && n->parent == n->grandparent()->left) {
      RotateRight(n->grandparent());
    } else {
      CHECK(n == n->parent->right && n->parent == n->grandparent()->right);
      RotateLeft(n->grandparent());
    }
  }

  // In this case, N has become the root node. The deletion removed one black
  // node from every path, so no properties are violated.
  void UnlinkCase1(T *n) {
    if (!n->parent) {
      return;
    }

    UnlinkCase2(n);
  }

  // N has a red sibling. In this case we exchange the colors of the parent and
  // sibling, then rotate about the parent so that the sibling becomes the
  // parent of its former parent. This does not restore the tree properties, but
  // reduces the problem to one of the remaining cases.
  void UnlinkCase2(T *n) {
    if (color(n->sibling()) == RED) {
      n->parent->color = RED;
      n->sibling()->color = BLACK;
      if (n == n->parent->left) {
        RotateLeft(n->parent);
      } else {
        RotateRight(n->parent);
      }
    }

    UnlinkCase3(n);
  }

  // In this case N's parent, sibling, and sibling's children are black. In this
  // case we paint the sibling red. Now all paths passing through N's parent
  // have one less black node than before the deletion, so we must recursively
  // run this procedure from case 1 on N's parent.
  void UnlinkCase3(T *n) {
    if (color(n->parent) == BLACK && color(n->sibling()) == BLACK &&
        color(n->sibling()->left) == BLACK &&
        color(n->sibling()->right) == BLACK) {
      n->sibling()->color = RED;
      UnlinkCase1(n->parent);
      return;
    }

    UnlinkCase4(n);
  }

  // N's sibling and sibling's children are black, but its parent is red. We
  // exchange the colors of the sibling and parent; this restores the tree
  // properties.
  void UnlinkCase4(T *n) {
    if (color(n->parent) == RED && color(n->sibling()) == BLACK &&
        color(n->sibling()->left) == BLACK &&
        color(n->sibling()->right) == BLACK) {
      n->sibling()->color = RED;
      n->parent->color = BLACK;
      return;
    }

    UnlinkCase5(n);
  }

  // There are two cases handled here which are mirror images of one another:
  // * N's sibling S is black, S's left child is red, S's right child is black,
  // and N is the left child of its parent. We exchange the colors of S and its
  // left sibling and rotate right at S.
  // * N's sibling S is black, S's right child is red, S's left child is black,
  // and N is the right child of its parent. We exchange the colors of S and its
  // right sibling and rotate left at S.
  // Both of these function to reduce us to the situation described in case 6.
  void UnlinkCase5(T *n) {
    if (n == n->parent->left && color(n->sibling()) == BLACK &&
        color(n->sibling()->left) == RED &&
        color(n->sibling()->right) == BLACK) {
      n->sibling()->color = RED;
      n->sibling()->left->color = BLACK;
      RotateRight(n->sibling());
    } else if (n == n->parent->right && color(n->sibling()) == BLACK &&
               color(n->sibling()->right) == RED &&
               color(n->sibling()->left) == BLACK) {
      n->sibling()->color = RED;
      n->sibling()->right->color = BLACK;
      RotateLeft(n->sibling());
    }

    UnlinkCase6(n);
  }

  // There are two cases handled here which are mirror images of one another:
  // * N's sibling S is black, S's right child is red, and N is the left child
  // of its parent. We exchange the colors of N's parent and sibling, make S's
  // right child black, then rotate left at N's parent.
  // * N's sibling S is black, S's left child is red, and N is the right child
  // of its parent. We exchange the colors of N's parent and sibling, make S's
  // left child black, then rotate right at N's parent.
  //
  // This accomplishes three things at once:
  // * We add a black node to all paths through N, either by adding a black S to
  // those paths or by recoloring N's parent black.
  // * We remove a black node from all paths through S's red child, either by
  // removing P from those paths or by recoloring S.
  // * We recolor S's red child black, adding a black node back to all paths
  // through S's red child.
  //
  // S's left child has become a child of N's parent during the rotation and so
  // is unaffected.
  void UnlinkCase6(T *n) {
    n->sibling()->color = color(n->parent);
    n->parent->color = BLACK;
    if (n == n->parent->left) {
      CHECK_EQ(color(n->sibling()->right), RED);
      n->sibling()->right->color = BLACK;
      RotateLeft(n->parent);
    } else {
      CHECK_EQ(color(n->sibling()->left), RED);
      n->sibling()->left->color = BLACK;
      RotateRight(n->parent);
    }
  }
};
}

#endif
