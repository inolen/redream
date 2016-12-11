/* would be nice to convert this file to C once MSVC supports stdatomic.h */
#include <atomic>
extern "C" {
#include "core/ringbuf.h"
#include "core/assert.h"
}

struct ringbuf {
  int capacity;
  uint8_t *data;
  std::atomic<int64_t> read_offset;
  std::atomic<int64_t> write_offset;
};

void ringbuf_advance_write_ptr(struct ringbuf *rb, int n) {
  rb->write_offset.fetch_add(n);
}

void *ringbuf_write_ptr(struct ringbuf *rb) {
  int64_t write_offset = rb->write_offset.load();
  return rb->data + (write_offset % rb->capacity);
}

void ringbuf_advance_read_ptr(struct ringbuf *rb, int n) {
  rb->read_offset.fetch_add(n);
}

void *ringbuf_read_ptr(struct ringbuf *rb) {
  int64_t read_offset = rb->read_offset.load();
  return rb->data + (read_offset % rb->capacity);
}

int ringbuf_remaining(struct ringbuf *rb) {
  return ringbuf_capacity(rb) - ringbuf_available(rb);
}

int ringbuf_available(struct ringbuf *rb) {
  int64_t read = rb->read_offset.load();
  int64_t write = rb->write_offset.load();
  int available = (int)(write - read);
  CHECK(available >= 0 && available <= rb->capacity);
  return available;
}

int ringbuf_capacity(struct ringbuf *rb) {
  return rb->capacity;
}

void ringbuf_destroy(struct ringbuf *rb) {
  free(rb);
}

struct ringbuf *ringbuf_create(int capacity) {
  struct ringbuf *rb = (struct ringbuf *)calloc(1, sizeof(struct ringbuf));

  rb->capacity = capacity;
  rb->data = (uint8_t *)malloc(capacity);

  return rb;
}
