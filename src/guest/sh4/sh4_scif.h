#ifndef SH4_SCIF_H
#define SH4_SCIF_H

#define SCIF_FIFO_SIZE 16

struct sh4_scif_fifo {
  int head;
  int tail;

  /* ringbuffers have an ambiguous case when the head is equal to the tail - the
     queue could be full or empty. add one to the fifo size to avoid this */
  uint8_t data[SCIF_FIFO_SIZE + 1];
};

#endif
