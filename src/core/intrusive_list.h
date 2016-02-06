#ifndef INTRUSIVE_LIST_H
#define INTRUSIVE_LIST_H

#include <stdlib.h>
#include <iterator>
#include <type_traits>
#include "core/assert.h"

namespace re {

// Objects are directly stored in the instrusive container, not copies. Due to
// this, the lifetime of the object is not bound to the container. It's up to
// the caller to manage the lifetime of the object being stored.
template <typename T>
class IntrusiveListNode {
  template <typename>
  friend class IntrusiveList;

 public:
  IntrusiveListNode() : prev_(nullptr), next_(nullptr) {}

  T *prev() { return prev_; }
  const T *prev() const { return prev_; }

  T *next() { return next_; }
  const T *next() const { return next_; }

 private:
  T *prev_;
  T *next_;
};

template <typename T>
class IntrusiveList {
  // For the iterator, remember that a C++ iterator's range is [begin, end),
  // meaning the end iterator will be wrapping an invalid node.
  template <bool is_const_iterator, bool is_reverse_iterator>
  class shared_iterator
      : public std::iterator<std::bidirectional_iterator_tag, T> {
    friend class IntrusiveList;

    typedef shared_iterator<is_const_iterator, is_reverse_iterator> self_type;
    typedef typename std::conditional<is_const_iterator, IntrusiveList const *,
                                      IntrusiveList *>::type list_pointer;
    typedef typename std::conditional<is_const_iterator, T const *, T *>::type
        pointer;

    static const intptr_t sentinel_end = 0xdeadbeef;

   public:
    // FIXME Can some of these nasty conditionals be removed?
    // is_reverse_iterator is known at compile time

    self_type &operator++() {
      node_ = is_reverse_iterator ? node_->prev() : node_->next();
      // if we've reached the end of the list, move onto the sentinel node
      if (!node_) {
        node_ = reinterpret_cast<T *>(sentinel_end);
      }
      return *this;
    }

    self_type operator++(int) {
      self_type old = *this;
      ++(*this);
      return old;
    }

    self_type &operator--() {
      // if we're at the sentinel node, the previous node is the list's tail
      if (node_ == reinterpret_cast<T *>(sentinel_end)) {
        node_ = is_reverse_iterator ? list_->head() : list_->tail();
      } else {
        node_ = is_reverse_iterator ? node_->next() : node_->prev();
      }
      return *this;
    }

    self_type operator--(int) {
      self_type old = *this;
      --(*this);
      return old;
    }

    pointer operator*() { return node_; }

    pointer operator->() { return node_; }

    bool operator==(const self_type &other) const {
      return node_ == other.node_;
    }

    bool operator!=(const self_type &other) const { return !(other == *this); }

   private:
    shared_iterator(list_pointer list, pointer node)
        : list_(list),
          node_(node ? node : reinterpret_cast<T *>(sentinel_end)) {}

    list_pointer list_;
    pointer node_;
  };

 public:
  typedef shared_iterator<false, false> iterator;
  typedef shared_iterator<true, false> const_iterator;
  typedef shared_iterator<false, true> reverse_iterator;
  typedef shared_iterator<true, true> const_reverse_iterator;

  // regular iterators
  const_iterator begin() const { return const_iterator(this, head_); }
  const_iterator end() const { return const_iterator(this, nullptr); }
  iterator begin() { return iterator(this, head_); }
  iterator end() { return iterator(this, nullptr); }

  // reverse iterators
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(this, tail_);
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(this, nullptr);
  }
  reverse_iterator rbegin() { return reverse_iterator(this, tail_); }
  reverse_iterator rend() { return reverse_iterator(this, nullptr); }

  const T *head() const { return head_; }
  const T *tail() const { return tail_; }

  T *head() { return head_; }
  T *tail() { return tail_; }

  IntrusiveList() : head_(nullptr), tail_(nullptr) {}

  void Prepend(T *v) { Insert(nullptr, v); }

  void Append(T *v) { Insert(tail_, v); }

  void Insert(T *after, T *v) {
    DCHECK_EQ(reinterpret_cast<T *>(NULL), v->prev_);
    DCHECK_EQ(reinterpret_cast<T *>(NULL), v->next_);

    // if after is null, insert at head
    if (!after) {
      if (head_) {
        v->next_ = head_;
        v->next_->prev_ = v;
      }

      head_ = v;
    } else {
      T *next = after->next_;

      v->prev_ = after;
      v->prev_->next_ = v;

      if (next) {
        v->next_ = next;
        v->next_->prev_ = v;
      } else {
        v->next_ = nullptr;
      }
    }

    if (!tail_ || after == tail_) {
      tail_ = v;
    }
  }

  void Remove(T *v) {
    if (v->prev_) {
      v->prev_->next_ = v->next_;
    } else {
      head_ = v->next_;
    }

    if (v->next_) {
      v->next_->prev_ = v->prev_;
    } else {
      tail_ = v->prev_;
    }

    v->prev_ = v->next_ = nullptr;
  }

  void Clear() { head_ = tail_ = nullptr; }

  // Implements the mergesort for linked lists as described at
  // http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
  template <class Compare>
  void Sort(Compare comp) {
    T *head = head_;
    T *tail = nullptr;
    int k = 1;

    while (true) {
      int merges = 0;
      T *p = head;

      head = nullptr;
      tail = nullptr;

      while (p) {
        // track the number of lists merged this pass
        merges++;

        // step q forward k places, tracking the size of p
        int psize = 0;
        int qsize = k;
        T *q = p;
        while (psize < k && q) {
          psize++;
          q = q->next_;
        }

        // merge the list starting at p of length psize with the list starting
        // at q of at most, length qsize
        while (psize || (qsize && q)) {
          T *next;

          if (!psize) {
            next = q;
            q = q->next_;
            qsize--;
          } else if (!qsize || !q) {
            next = p;
            p = p->next_;
            psize--;
          } else if (comp(q, p)) {
            next = q;
            q = q->next_;
            qsize--;
          } else {
            next = p;
            p = p->next_;
            psize--;
          }

          // move merged node to tail
          if (!tail) {
            head = tail = next;
          } else {
            tail->next_ = next;
            tail = tail->next_;
          }
        }

        p = q;
      }

      if (tail) {
        tail->next_ = nullptr;
      }

      // if only 1 pair of lists was merged, this is the end
      if (merges <= 1) {
        break;
      }

      k *= 2;
    }

    // update internal head and tail with sorted head and tail
    head_ = head;
    tail_ = tail;
  }

 private:
  T *head_;
  T *tail_;
};
}

#endif
