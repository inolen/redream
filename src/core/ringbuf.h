#ifndef RINGBUF_H
#define RINGBUF_H

struct ringbuf;

struct ringbuf *ringbuf_create(int capacity);
void ringbuf_destroy(struct ringbuf *rb);

int ringbuf_capacity(struct ringbuf *rb);
int ringbuf_available(struct ringbuf *rb);
int ringbuf_remaining(struct ringbuf *rb);

void ringbuf_clear(struct ringbuf *rb);

void *ringbuf_read_ptr(struct ringbuf *rb);
void ringbuf_advance_read_ptr(struct ringbuf *rb, int n);

void *ringbuf_write_ptr(struct ringbuf *rb);
void ringbuf_advance_write_ptr(struct ringbuf *rb, int n);

#endif
