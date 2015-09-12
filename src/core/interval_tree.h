#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H

#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <assert.h>
#include <stdlib.h>
#include "core/assert.h"

namespace dreavm {

// Interval tree implemented using a randomized bst. Based on implementation at
// http://algs4.cs.princeton.edu/93intersection/IntervalST.java
//
// Parent pointers have been added in order to make removal by node (as opposed
// to key) possible.

template <typename IT, typename VT>
class IntervalTree {
 public:
  struct Node;

  typedef IT interval_type;
  typedef VT value_type;
  typedef IntervalTree<interval_type, value_type> self_type;
  typedef std::function<void(const self_type &, Node *)> iterate_cb;

  struct Node {
    Node(const interval_type &low, const interval_type &high,
         const value_type &value)
        : parent(nullptr),
          left(nullptr),
          right(nullptr),
          low(low),
          high(high),
          max(high),
          value(value),
          num(1) {}

    bool operator<(const Node &rhs) const {
      return low < rhs.low || (low == rhs.low && high < rhs.high);
    }

    Node *parent, *left, *right;
    interval_type low, high, max;
    value_type value;
    int num;
  };

  IntervalTree() : root_(nullptr) {}

  ~IntervalTree() { Clear(); }

  int Size() { return Size(root_); }

  int Height() { return Height(root_); }

  Node *Insert(const interval_type &low, const interval_type &high,
               const value_type &value) {
    Node *n = new Node(low, high, value);

    SetRoot(RandomizedInsert(root_, n));

    return n;
  }

  void Remove(Node *n) {
    // join left and right subtrees, assign new joined subtree to parent
    Node *joined = Join(n->left, n->right);

    if (!n->parent) {
      // removed node had no parent, must have been root
      CHECK_EQ(root_, n);
      SetRoot(joined);
    } else if (n->parent->left == n) {
      SetLeft(n->parent, joined);
    } else {
      SetRight(n->parent, joined);
    }

    // fix up each node in the parent chain
    Node *parent = n->parent;
    while (parent) {
      FixCounts(parent);
      parent = parent->parent;
    }

    // remove the node
    delete n;
  }

  void Clear() {
    Clear(root_);

    SetRoot(nullptr);
  }

  Node *Find(interval_type low, interval_type high) {
    Node *n = root_;

    while (n) {
      if (high >= n->low && n->high >= low) {
        return n;
      } else if (!n->left || n->left->max < low) {
        n = n->right;
      } else {
        n = n->left;
      }
    }

    return nullptr;
  }

  void Iterate(interval_type low, interval_type high, iterate_cb cb) {
    Iterate(root_, low, high, cb);
  }

 private:
  int Size(Node *n) { return n ? n->num : 0; }

  int Height(Node *n) {
    return n ? 1 + std::max(Height(n->left), Height(n->right)) : 0;
  }

  //
  // insertion
  //
  Node *RootInsert(Node *root, Node *n) {
    if (!root) {
      return n;
    }

    if (*n < *root) {
      SetLeft(root, RootInsert(root->left, n));
      root = RotateRight(root);
    } else {
      SetRight(root, RootInsert(root->right, n));
      root = RotateLeft(root);
    }

    return root;
  }

  Node *RandomizedInsert(Node *root, Node *n) {
    if (!root) {
      return n;
    }

    // make new node the root with uniform probability
    if (rand() % (Size(root) + 1) == 0) {
      return RootInsert(root, n);
    }

    if (*n < *root) {
      SetLeft(root, RandomizedInsert(root->left, n));
    } else {
      SetRight(root, RandomizedInsert(root->right, n));
    }

    return root;
  }

  //
  // removal
  //
  void Clear(Node *n) {
    if (!n) {
      return;
    }

    Clear(n->left);
    Clear(n->right);

    delete n;
  }

  //
  // iteration
  //
  bool Iterate(Node *n, interval_type low, interval_type high, iterate_cb cb) {
    if (!n) {
      return false;
    }

    bool found1 = false;
    bool found2 = false;
    bool found3 = false;

    if (high >= n->low && n->high >= low) {
      cb(*this, n);
      found1 = true;
    }

    if (n->left && n->left->max >= low) {
      found2 = Iterate(n->left, low, high, cb);
    }

    if (found2 || !n->left || n->left->max < low) {
      found3 = Iterate(n->right, low, high, cb);
    }

    return found1 || found2 || found3;
  }

  //
  // helper methods
  //
  void SetRoot(Node *n) {
    root_ = n;

    if (root_) {
      root_->parent = nullptr;
    }
  }

  void SetLeft(Node *parent, Node *n) {
    parent->left = n;

    if (parent->left) {
      parent->left->parent = parent;
    }

    FixCounts(parent);
  }

  void SetRight(Node *parent, Node *n) {
    parent->right = n;

    if (parent->right) {
      parent->right->parent = parent;
    }

    FixCounts(parent);
  }

  void FixCounts(Node *n) {
    if (!n) {
      return;
    }

    n->num = 1 + Size(n->left) + Size(n->right);
    n->max = n->high;
    if (n->left) {
      n->max = std::max(n->max, n->left->max);
    }
    if (n->right) {
      n->max = std::max(n->max, n->right->max);
    }
  }

  Node *RotateRight(Node *root) {
    Node *parent = root->parent;
    Node *n = root->left;

    SetLeft(root, n->right);
    SetRight(n, root);

    if (parent) {
      if (parent->left == root) {
        SetLeft(parent, n);
      } else {
        SetRight(parent, n);
      }
    }

    return n;
  }

  Node *RotateLeft(Node *root) {
    Node *parent = root->parent;
    Node *n = root->right;

    SetRight(root, n->left);
    SetLeft(n, root);

    if (parent) {
      if (parent->left == root) {
        SetLeft(parent, n);
      } else {
        SetRight(parent, n);
      }
    }

    return n;
  }

  Node *Join(Node *a, Node *b) {
    if (!a) {
      return b;
    } else if (!b) {
      return a;
    }

    if ((rand() % (Size(a) + Size(b))) < Size(a)) {
      SetRight(a, Join(a->right, b));
      return a;
    } else {
      SetLeft(b, Join(a, b->left));
      return b;
    }
  }

  Node *root_;
};
}

#endif
