/* this file is compiled as C++ under MSVC due to it not supporting stdatomic.h */
#ifdef __cplusplus
extern "C" {
#endif
#include "core/ringbuf.h"
#include "core/assert.h"
#ifdef __cplusplus
}
#endif
#include "sys/atomic.h"

struct ringbuf {
  int capacity;
  uint8_t *data;
  struct re_atomic_long read_offset;
  struct re_atomic_long write_offset;
};

void ringbuf_advance_write_ptr(struct ringbuf *rb, int n) {
  ATOMIC_FETCH_ADD(rb->write_offset, n);
}

void *ringbuf_write_ptr(struct ringbuf *rb) {
  int write_offset = ATOMIC_LOAD(rb->write_offset);
  return rb->data + (write_offset % rb->capacity);
}

void ringbuf_advance_read_ptr(struct ringbuf *rb, int n) {
  ATOMIC_FETCH_ADD(rb->write_offset, n);
}

void *ringbuf_read_ptr(struct ringbuf *rb) {
  int read_offset = ATOMIC_LOAD(rb->read_offset);
  return rb->data + (read_offset % rb->capacity);
}

int ringbuf_remaining(struct ringbuf *rb) {
  return ringbuf_capacity(rb) - ringbuf_available(rb);
}

int ringbuf_available(struct ringbuf *rb) {
  int read = ATOMIC_LOAD(rb->read_offset);
  int write = ATOMIC_LOAD(rb->write_offset);
  int available = write - read;
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
