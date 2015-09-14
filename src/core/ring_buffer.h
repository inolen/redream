#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <iterator>
#include "core/assert.h"

namespace dreavm {

template <typename T>
class RingBuffer {
  template <bool is_const_iterator>
  class shared_iterator
      : public std::iterator<std::random_access_iterator_tag, T> {
    friend class RingBuffer;

   public:
    typedef RingBuffer<T> *parent_type;
    typedef shared_iterator<is_const_iterator> self_type;
    typedef int difference_type;
    typedef typename std::conditional<is_const_iterator, T const, T>::type
        reference;
    typedef typename std::conditional<is_const_iterator, T const *, T *>::type
        pointer;

    self_type &operator++() {
      index_++;
      return *this;
    }

    self_type operator++(int) {
      self_type old(*this);
      ++(*this);
      return old;
    }

    self_type &operator--() {
      index_--;
      return *this;
    }

    self_type operator--(int) {
      self_type old(*this);
      --(*this);
      return old;
    }

    // support std::distance
    difference_type operator-(const self_type &other) {
      return static_cast<difference_type>(index_ - other.index_);
    }

    // support std::advance
    void operator+=(difference_type diff) { index_ += diff; }

    reference operator*() { return (*parent_)[index_]; }

    pointer operator->() { return &(*parent_)[index_]; }

    bool operator==(const self_type &other) const {
      return parent_ == other.parent_ && index_ == other.index_;
    }

    bool operator!=(const self_type &other) const { return !(other == *this); }

   private:
    shared_iterator(parent_type parent, size_t index)
        : parent_(parent), index_(index) {}

    parent_type parent_;
    size_t index_;
  };

 public:
  typedef shared_iterator<false> iterator;
  typedef shared_iterator<true> const_iterator;

  const_iterator begin() const { return const_iterator(this, 0); }
  const_iterator end() const { return const_iterator(this, Size()); }
  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, Size()); }

  RingBuffer(size_t max) : max_(max), front_(0), back_(0) {
    buffer_ = new T[max_];
  }
  ~RingBuffer() { delete[] buffer_; }

  const T &operator[](size_t index) const {
    return buffer_[(front_ + index) % max_];
  }
  T &operator[](size_t index) { return buffer_[(front_ + index) % max_]; }

  const T &front() const { return buffer_[front_ % max_]; }
  T &front() { return buffer_[front_ % max_]; }

  const T &back() const { return buffer_[(back_ - 1) % max_]; }
  T &back() { return buffer_[(back_ - 1) % max_]; }

  size_t Size() const { return back_ - front_; }

  bool Empty() const { return !Size(); }

  bool Full() const { return Size() >= max_; }

  void Clear() { back_ = front_ = 0; }

  void PushBack(const T &el) {
    // if the buffer is wrapping, advance front_
    if (back_ - front_ >= max_) {
      front_++;
    }

    buffer_[back_++ % max_] = el;
  }

  void PopBack() {
    DCHECK(back_ > front_);
    back_--;
  }

  void PopFront() {
    DCHECK(front_ < back_);
    front_++;
  }

  void Insert(const iterator &it, const T &el) {
    size_t end = front_ + max_ - 1;

    // if the buffer isn't full, increase its size
    if (back_ < end) {
      end = back_++;
    }

    // shift old elements over by one
    while ((end - front_) != it.index_) {
      buffer_[end % max_] = buffer_[(end - 1) % max_];
      end--;
    }

    // add new element
    buffer_[end % max_] = el;
  }

 private:
  T *buffer_;
  const size_t max_;
  size_t front_;
  size_t back_;
};
}

#endif
