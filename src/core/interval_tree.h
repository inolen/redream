#ifndef INTERVAL_TREE_H
#define INTERVAL_TREE_H

#include <algorithm>
#include <functional>
#include <utility>
#include <stdlib.h>
#include "core/assert.h"
#include "core/intrusive_tree.h"

namespace dreavm {

typedef uintptr_t interval_type;

template <typename T>
struct IntervalNode : public IntrusiveTreeNode<IntervalNode<T>> {
  IntervalNode(const interval_type &low, const interval_type &high,
               const T &value)
      : low(low), high(high), max(high), value(value), num(1), height(1) {}

  bool Intersects(const interval_type &low, const interval_type &high) {
    return high >= this->low && this->high >= low;
  }

  bool operator<(const IntervalNode<T> &rhs) const {
    return low < rhs.low || (low == rhs.low && high < rhs.high);
  }

  interval_type low, high, max;
  T value;
  int num, height;
};

template <typename T>
class IntervalTree : public IntrusiveTree<IntervalNode<T>> {
  template <bool is_const_iterator>
  class shared_range_iterator
      : public std::iterator<std::forward_iterator_tag, T> {
    friend class IntervalTree<T>;

    typedef shared_range_iterator<is_const_iterator> self_type;
    typedef
        typename std::conditional<is_const_iterator, const IntervalTree<T> *,
                                  IntervalTree<T> *>::type tree_pointer;
    typedef
        typename std::conditional<is_const_iterator, const IntervalNode<T> *,
                                  IntervalNode<T> *>::type node_pointer;

   public:
    self_type &operator++() {
      node_ = tree_->NextInterval(node_, low_, high_);
      return *this;
    }

    self_type operator++(int) {
      self_type old = *this;
      ++(*this);
      return old;
    }

    node_pointer operator*() { return node_; }

    node_pointer operator->() { return node_; }

    bool operator==(const self_type &other) const {
      return node_ == other.node_;
    }

    bool operator!=(const self_type &other) const { return !(other == *this); }

   private:
    shared_range_iterator(tree_pointer tree, const interval_type &low,
                          const interval_type &high, node_pointer node)
        : tree_(tree), low_(low), high_(high), node_(node) {}

    tree_pointer tree_;
    interval_type low_;
    interval_type high_;
    node_pointer node_;
  };

 public:
  typedef IntervalTree<T> self_type;
  typedef IntervalNode<T> node_type;
  typedef std::function<void(const self_type &, node_type *)> iterate_cb;

  typedef shared_range_iterator<false> range_iterator;
  typedef shared_range_iterator<true> const_range_iterator;

  ~IntervalTree() { Clear(); }

  std::pair<const_range_iterator, const_range_iterator> cintersect(
      const interval_type &low, const interval_type &high) const {
    return std::pair<const_range_iterator, const_range_iterator>(
        const_range_iterator(this, low, high,
                             MinInterval(this->root_, low, high)),
        const_range_iterator(this, low, high, nullptr));
  }
  std::pair<range_iterator, range_iterator> intersect(
      const interval_type &low, const interval_type &high) {
    return std::pair<range_iterator, range_iterator>(
        range_iterator(this, low, high, MinInterval(this->root_, low, high)),
        range_iterator(this, low, high, nullptr));
  }

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
      if (n->Intersects(low, high)) {
        return n;
      } else if (!n->left || n->left->max < low) {
        n = n->right;
      } else {
        n = n->left;
      }
    }

    return nullptr;
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

  node_type *MinInterval(node_type *n, const interval_type &low,
                         const interval_type &high) {
    node_type *min = nullptr;

    while (n) {
      bool intersects = n->Intersects(low, high);
      if (intersects) {
        min = n;
      }

      // if n->left->max < low, there is no match in the left subtree, there
      // could be one in the right
      if (!n->left || n->left->max < low) {
        // don't go right if the current node intersected
        if (intersects) {
          break;
        }
        n = n->right;
      }
      // else if n->left-max >= low, there could be one in the left subtree. if
      // there isn't one in the left, there wouldn't be one in the right
      else {
        n = n->left;
      }
    }

    return min;
  }

  node_type *NextInterval(node_type *n, const interval_type &low,
                          const interval_type &high) {
    while (n) {
      // try to find the minimum node in the right subtree
      if (n->right) {
        node_type *min = MinInterval(n->right, low, high);
        if (min) {
          return min;
        }
      }

      // else, move up the tree until a left child link is traversed
      node_type *c = n;
      n = n->parent;
      while (n && n->right == c) {
        c = n;
        n = n->parent;
      }
      if (n && n->Intersects(low, high)) {
        return n;
      }
    }

    return nullptr;
  }
};
}

#endif
