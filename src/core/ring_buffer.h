#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <atomic>
#include <glog/logging.h>

namespace dreavm {
namespace core {

template <typename T, unsigned MAX>
class RingBuffer {
 public:
  RingBuffer() : head(0), tail(0) {}

  RingBuffer(const RingBuffer &other)
      : buffer(other.buffer), head(other.head), tail(other.tail) {}

  RingBuffer &operator=(const RingBuffer &other) { return *this; }

  const T &front() const { return buffer[tail % MAX]; }

  T &front() { return buffer[tail % MAX]; }

  const T &back() const {
    DCHECK(head);
    return buffer[(head - 1) % MAX];
  }

  T &back() {
    DCHECK(head);
    return buffer[(head - 1) % MAX];
  }

  void push_back(const T &el) {
    buffer[head % MAX] = el;
    ++head;
  }

  void pop_front() {
    DCHECK(tail < head);
    ++tail;
  }

  size_t size() const { return head - tail; }

  bool empty() const { return !size(); }

  bool full() const { return size() >= MAX; }

 private:
  T buffer[MAX];
  std::atomic<unsigned> head;
  std::atomic<unsigned> tail;
};
}
}

#endif
