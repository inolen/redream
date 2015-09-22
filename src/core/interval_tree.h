#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H

#include <algorithm>
#include <functional>
#include <stdlib.h>
#include "core/assert.h"
#include "core/intrusive_tree.h"

namespace dreavm {

typedef int64_t interval_type;

template <typename T>
struct IntervalNode : public IntrusiveTreeNode<IntervalNode<T>> {
  IntervalNode(const interval_type &low, const interval_type &high,
               const T &value)
      : low(low), high(high), max(high), value(value), num(1), height(1) {}

  bool operator<(const IntervalNode<T> &rhs) const {
    return low < rhs.low || (low == rhs.low && high < rhs.high);
  }

  interval_type low, high, max;
  T value;
  int num, height;
};

template <typename T>
class IntervalTree : public IntrusiveTree<IntervalNode<T>> {
 public:
  typedef IntervalTree<T> self_type;
  typedef IntervalNode<T> node_type;
  typedef std::function<void(const self_type &, node_type *)> iterate_cb;

  ~IntervalTree() { Clear(); }

  int Size() { return Size(this->root_); }

  int Height() { return Height(this->root_); }

  node_type *Insert(const interval_type &low, const interval_type &high,
                    const T &value) {
    node_type *n = new node_type(low, high, value);

    // add new node into the correct location in the tree, then link it in
    // to recolor the tree
    node_type *parent = this->root_;
    while (parent) {
      if (*n < *parent) {
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

    this->Link(n);

    return n;
  }

  void Remove(node_type *n) {
    this->Unlink(n);

    delete n;
  }

  void Clear() {
    Clear(this->root_);

    this->root_ = nullptr;
  }

  node_type *Find(interval_type low, interval_type high) {
    node_type *n = this->root_;

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
    Iterate(this->root_, low, high, cb);
  }

 protected:
  void AugmentPropagate(node_type *n) {
    while (n) {
      FixCounts(n);
      n = n->parent;
    }
  }

  void AugmentRotate(node_type *oldn, node_type *newn) {
    FixCounts(oldn);
    FixCounts(newn);
    FixCounts(newn->parent);
  }

 private:
  int Size(node_type *n) { return n ? n->num : 0; }
  int Height(node_type *n) { return n ? n->height : 0; }
  interval_type Max(node_type *n) { return n ? n->max : 0; }

  void FixCounts(node_type *n) {
    if (!n) {
      return;
    }

    n->num = 1 + Size(n->left) + Size(n->right);
    n->height = 1 + std::max(Height(n->left), Height(n->right));
    n->max = std::max(std::max(n->high, Max(n->left)), Max(n->right));
  }

  void Clear(node_type *n) {
    if (!n) {
      return;
    }

    Clear(n->left);
    Clear(n->right);

    delete n;
  }

  bool Iterate(node_type *n, interval_type low, interval_type high,
               iterate_cb cb) {
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
};
}

#endif
