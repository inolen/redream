#ifndef ARRAY_H
#define ARRAY_H

#include <string.h>
#include <stdlib.h>

namespace re {

template <typename T>
class array {
 public:
  array(int size = 8) : data_(nullptr), size_(0), capacity_(0) {
    Resize(size);
  }
  ~array() { free(data_); }

  array(array const &) = delete;
  void operator=(array const &) = delete;

  T &operator[](int i) { return data_[i]; }
  T operator[](int i) const { return data_[i]; }

  T *data() { return data_; }
  const T *data() const { return data_; }

  T &front() { return data_[0]; }
  T &back() { return data_[size_ - 1]; }

  int size() const { return size_; }
  bool empty() const { return !!size_; }
  int capacity() const { return capacity_; }

  void Resize(int size) {
    Reserve(size);
    size_ = size;
  }

  void Reserve(int cap) {
    if (capacity_ >= cap) {
      return;
    }

    // grow capacity to be >= cap
    if (!capacity_) {
      capacity_ = 1;
    }
    while (capacity_ < cap) {
      capacity_ *= 2;
    }

    data_ = reinterpret_cast<T *>(realloc(data_, capacity_ * sizeof(T)));
  }

  void Clear() { size_ = 0; }

  void PushBack(T v) { data_[size_++] = v; }

  void PopBack() { size_--; }

 private:
  T *data_;
  int size_;
  int capacity_;
};
}

#endif
