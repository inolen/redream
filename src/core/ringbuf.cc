/* would be nice to convert this file to C once MSVC supports stdatomic.h */
#include <atomic>
extern "C" {
#include "core/core.h"
#include "core/memory.h"
#include "core/ringbuf.h"
}

/* single producer, single consumer ring buffer implementation */
struct ringbuf {
  shmem_handle_t shmem;
  int size;
  uint8_t *data;
  std::atomic<int64_t> read_offset;
  std::atomic<int64_t> write_offset;
};

void ringbuf_advance_write_ptr(struct ringbuf *rb, int n) {
  /* perform release to prevent the advance from occurring before the data is
     is written to the ring buffer, for example:

     void *write_ptr = ringbuf_write_ptr(rb);
     memcpy(write_ptr, data, size);
     ringbuf_advance_write_ptr(rb, size);

     without the release, the memcpy could be reordered to occur after the
     advance, leaving the consumer to read garbage data */
  rb->write_offset.fetch_add(n, std::memory_order_release);
  DCHECK(ringbuf_remaining(rb) >= 0);
}

void *ringbuf_write_ptr(struct ringbuf *rb) {
  /* relaxed ordering is fine here as there is only a single thread writing to
     write_offset  */
  int64_t write_offset = rb->write_offset.load();
  return rb->data + (write_offset % rb->size);
}

void ringbuf_advance_read_ptr(struct ringbuf *rb, int n) {
  /* perform release to prevent the advance from occurring before the data is
     is read from the ring buffer, for example:

     void *read_ptr = ringbuf_read_ptr(rb);
     memcpy(data, read_ptr, size);
     ringbuf_advance_read_ptr(rb, size);

     without the release, the advance could be reordered to occur before the
     memcpy, at which point the producer could start writing over data that's
     not yet been read */
  rb->read_offset.fetch_add(n, std::memory_order_release);
  DCHECK(ringbuf_remaining(rb) >= 0);
}

void *ringbuf_read_ptr(struct ringbuf *rb) {
  /* relaxed ordering is fine here as there is only a single thread writing to
     read_offset */
  int64_t read_offset = rb->read_offset.load();
  return rb->data + (read_offset % rb->size);
}

int ringbuf_remaining(struct ringbuf *rb) {
  return ringbuf_size(rb) - ringbuf_available(rb);
}

int ringbuf_available(struct ringbuf *rb) {
  /* it should be safe to perform both of these loads with relaxed ordering as
     both offsets only advance forward

     in the case that the producer reads an old value for read_offset, it'd
     think there was less room to write data than there actually is

     in the case that the consumer reads an old value for the write_offset, it'd
     think there was less data to be read than there actually is */
  int64_t read = rb->read_offset.load();
  int64_t write = rb->write_offset.load();
  int available = (int)(write - read);
  DCHECK(available >= 0 && available <= rb->size);
  return available;
}

int ringbuf_size(struct ringbuf *rb) {
  return rb->size;
}

void ringbuf_destroy(struct ringbuf *rb) {
  unmap_shared_memory(rb->shmem, 0, rb->size * 2);

  destroy_shared_memory(rb->shmem);

  free(rb);
}

struct ringbuf *ringbuf_create(int size) {
  struct ringbuf *rb = (struct ringbuf *)calloc(1, sizeof(struct ringbuf));

  /* round up size to the next allocation granularity multiple */
  int page_size = (int)get_allocation_granularity();
  rb->size = ALIGN_UP(size, page_size);

  /* create shared memory object that will back the buffer */
  char label[128];
  snprintf(label, sizeof(label), "/ringbuf_%p", rb);
  rb->shmem = create_shared_memory(label, rb->size, ACC_READWRITE);
  CHECK_NE(rb->shmem, SHMEM_INVALID);

  /* map the buffer twice, back to back, such that no conditional operations
     have to be performed when reading / writing off the end of the buffer */
  rb->data = (uint8_t *)reserve_pages(NULL, rb->size * 2);
  CHECK_NOTNULL(rb->data);

  /* release pages and hope nothing maps to them before subsequent map */
  int res = release_pages(rb->data, rb->size * 2);
  CHECK_EQ(res, 1);

  void *target = rb->data;
  void *ptr = map_shared_memory(rb->shmem, 0, target, rb->size, ACC_READWRITE);
  CHECK_EQ(ptr, target);

  target = rb->data + rb->size;
  ptr = map_shared_memory(rb->shmem, 0, target, rb->size, ACC_READWRITE);
  CHECK_EQ(ptr, target);

  return rb;
}
