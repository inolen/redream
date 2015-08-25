#ifndef ARENA_H
#define ARENA_H

#include <stdint.h>
#include <stdlib.h>
#include "core/assert.h"

namespace dreavm {
namespace core {

class Arena {
  struct Chunk {
    Chunk(size_t capacity) : capacity(capacity), head(0), next(nullptr) {
      buffer = (uint8_t *)malloc(capacity);
    }

    ~Chunk() { free(buffer); }

    size_t capacity;
    uint8_t *buffer;
    size_t head;
    Chunk *next;
  };

 public:
  Arena(size_t chunk_size)
      : chunk_size_(chunk_size), root_chunk_(nullptr), current_chunk_(nullptr) {
    current_chunk_ = root_chunk_ = new Chunk(chunk_size_);
  }

  ~Arena() {
    Chunk *chunk = root_chunk_;

    while (chunk) {
      Chunk *next = chunk->next;
      delete chunk;
      chunk = next;
    }
  }

  void *Alloc(size_t bytes) {
    CHECK_LE(bytes, chunk_size_,
             "Allocation of %zu bytes is greater than chunk size of %zu bytes",
             bytes, chunk_size_);

    // alloc the next chunk if we're out of capacity
    if ((current_chunk_->capacity - current_chunk_->head) < bytes) {
      Chunk *next = current_chunk_->next;
      if (!next) {
        next = new Chunk(chunk_size_);
        current_chunk_ = next;
      }
      current_chunk_ = next;
    }

    void *ptr = current_chunk_->buffer + current_chunk_->head;
    current_chunk_->head += bytes;
    return ptr;
  }

  template <typename T>
  T *Alloc() {
    return (T *)Alloc(sizeof(T));
  }

  void Reset() {
    current_chunk_ = root_chunk_;
    current_chunk_->head = 0;
  }

 private:
  size_t chunk_size_;
  Chunk *root_chunk_, *current_chunk_;
};
}
}

#endif
