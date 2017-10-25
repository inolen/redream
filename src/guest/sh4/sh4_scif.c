/*
 * serial communication interface implementation
 *
 * this implementation is very incomplete. primarily, the serial port's transfer
 * rate is not emulated, transfers are instead pumped when the status register
 * is polled. due to this, features like overrun are also not emulated, it's
 * just made to never occur
 *
 * with that said, the implemntation is complete enough to communicate with
 * dcload, which is the primary use case
 */

#include "guest/serial/serial.h"
#include "guest/sh4/sh4.h"

static int fifo_size(struct sh4_scif_fifo *q) {
  int size = q->head - q->tail;

  /* check if the head pointer has wrapped around */
  if (size < 0) {
    size = sizeof(q->data) - (q->tail - q->head);
  }

  return size;
}

static int fifo_dequeue(struct sh4_scif_fifo *q) {
  if (q->tail == q->head) {
    return -1;
  }

  int data = q->data[q->tail];
  q->tail = (q->tail + 1) % sizeof(q->data);
  return data;
}

static int fifo_enqueue(struct sh4_scif_fifo *q, int data) {
  int size = fifo_size(q);

  /* never let the fifo completely fill up (see sh4_scif.h) */
  if (size == SCIF_FIFO_SIZE) {
    return 0;
  }

  q->data[q->head] = data;
  q->head = (q->head + 1) % sizeof(q->data);
  return 1;
}

static int receive_triggered(struct sh4 *sh4) {
  static uint32_t receive_triggers[] = {1, 4, 8, 14};
  return sh4->SCFDR2->R >= receive_triggers[sh4->SCFCR2->RTRG];
}

static int receive_dequeue(struct sh4 *sh4) {
  struct sh4_scif_fifo *q = &sh4->receive_fifo;

  int data = fifo_dequeue(q);
  if (data == -1) {
    return -1;
  }

  sh4->SCFDR2->R = fifo_size(q);

  /* RDF isn't cleared when reading from SCFRDR2, it must be explicitly cleared
     by writing to SCFSR2 */

  return data;
}

static int receive_enqueue(struct sh4 *sh4, int data) {
  struct sh4_scif_fifo *q = &sh4->receive_fifo;

  /* TODO raise ORER on overflow */
  int res = fifo_enqueue(q, data);
  CHECK(res);

  sh4->SCFDR2->R = fifo_size(q);
  sh4->SCFSR2->RDF = receive_triggered(sh4);

  /* raise interrupt if enabled and triggered */
  if (sh4->SCSCR2->RIE && sh4->SCFSR2->RDF) {
    sh4_raise_interrupt(sh4, SH4_INT_SCIFRXI);
  }

  return 1;
}

static void receive_reset(struct sh4 *sh4) {
  struct sh4_scif_fifo *q = &sh4->receive_fifo;
  q->head = q->tail = 0;
  sh4->SCFDR2->R = 0;
  sh4->SCFSR2->RDF = 0;
  sh4_clear_interrupt(sh4, SH4_INT_SCIFRXI);
}

static int transmit_triggered(struct sh4 *sh4) {
  static uint32_t transmit_triggers[] = {8, 4, 2, 1};
  return sh4->SCFDR2->T <= transmit_triggers[sh4->SCFCR2->TTRG];
}

static int transmit_ended(struct sh4 *sh4) {
  return sh4->SCFDR2->T == 0;
}

static int transmit_dequeue(struct sh4 *sh4) {
  struct sh4_scif_fifo *q = &sh4->transmit_fifo;

  int data = fifo_dequeue(q);
  if (data == -1) {
    return -1;
  }

  sh4->SCFDR2->T = fifo_size(q);
  sh4->SCFSR2->TDFE = transmit_triggered(sh4);
  sh4->SCFSR2->TEND = transmit_ended(sh4);

  /* raise interrupt if enabled and triggered */
  if (sh4->SCSCR2->TIE && sh4->SCFSR2->TDFE) {
    sh4_raise_interrupt(sh4, SH4_INT_SCIFTXI);
  }

  return data;
}

static int transmit_enqueue(struct sh4 *sh4, int data) {
  struct sh4_scif_fifo *q = &sh4->transmit_fifo;

  /* TODO discard when full */
  int res = fifo_enqueue(q, data);
  CHECK(res);

  sh4->SCFDR2->T = fifo_size(q);

  /* TDFE isn't cleared when writing SCFTDR2, it must be explicitly cleared
     by writing to SCFSR2 */

  return 1;
}

static void transmit_reset(struct sh4 *sh4) {
  struct sh4_scif_fifo *q = &sh4->transmit_fifo;
  q->head = q->tail = 0;
  sh4->SCFDR2->T = 0;
  sh4->SCFSR2->TEND = 1;
  sh4->SCFSR2->TDFE = 1;
  sh4_clear_interrupt(sh4, SH4_INT_SCIFTXI);
}

static void sh4_scif_run(struct sh4 *sh4) {
  struct serial *serial = sh4->dc->serial;

  if (!serial) {
    return;
  }

  /* transfer rates aren't emulated at all, just completely fill / drain each
     queue at this point */
  if (sh4->SCSCR2->RE && !sh4->SCLSR2->ORER) {
    while (sh4->SCFDR2->R < SCIF_FIFO_SIZE) {
      int data = serial_getchar(serial);

      if (data == -1) {
        break;
      }

      receive_enqueue(sh4, data);
    }
  }

  if (sh4->SCSCR2->TE) {
    while (sh4->SCFDR2->T > 0) {
      int data = transmit_dequeue(sh4);

      CHECK_NE(data, -1);

      serial_putchar(serial, data);
    }
  }
}

REG_W32(sh4_cb, SCSMR2) {
  struct sh4 *sh4 = dc->sh4;
  sh4->SCSMR2->full = value;

  /* none of the fancy transfer modes are supported */
  CHECK_EQ(sh4->SCSMR2->full, 0);
}

REG_W32(sh4_cb, SCBRR2) {
  struct sh4 *sh4 = dc->sh4;

  /* TODO handle transfer rate */

  *sh4->SCBRR2 = value;
}

REG_W32(sh4_cb, SCSCR2) {
  struct sh4 *sh4 = dc->sh4;
  sh4->SCSCR2->full = value;

  CHECK_EQ(sh4->SCSCR2->CKE1, 0);

  /* transmission has ended */
  if (!sh4->SCSCR2->TE) {
    sh4->SCFSR2->TEND = 1;
  }

  /* clear interrupts if disabled */
  if (!sh4->SCSCR2->REIE && !sh4->SCSCR2->RIE) {
    sh4_clear_interrupt(sh4, SH4_INT_SCIFERI);
    sh4_clear_interrupt(sh4, SH4_INT_SCIFBRI);
  }
  if (!sh4->SCSCR2->RIE) {
    sh4_clear_interrupt(sh4, SH4_INT_SCIFRXI);
  }
  if (!sh4->SCSCR2->TIE) {
    sh4_clear_interrupt(sh4, SH4_INT_SCIFTXI);
  }
}

REG_R32(sh4_cb, SCFTDR2) {
  LOG_FATAL("unexpected read from SCFTDR2");
}

REG_W32(sh4_cb, SCFTDR2) {
  struct sh4 *sh4 = dc->sh4;
  transmit_enqueue(sh4, (int)value);
}

REG_R32(sh4_cb, SCFSR2) {
  struct sh4 *sh4 = dc->sh4;

  sh4_scif_run(sh4);

  /* in order to clear the SCFSR2 bits, they must be read first */
  sh4->SCFSR2_last_read = sh4->SCFSR2->full;

  return sh4->SCFSR2->full;
}

REG_W32(sh4_cb, SCFSR2) {
  struct sh4 *sh4 = dc->sh4;

  /* can only clear ER, TEND, TDFE, BRK, RDF and DR */
  value |= 0xffffff0c;

  /* can only clear if the flag was previously read as 1 */
  value |= ~sh4->SCFSR2_last_read;

  sh4->SCFSR2->full &= value;

  /* RDF / TDFE / TEND aren't cleared if still valid */
  sh4->SCFSR2->RDF = receive_triggered(sh4);
  sh4->SCFSR2->TDFE = transmit_triggered(sh4);
  sh4->SCFSR2->TEND = transmit_ended(sh4);

  /* clear RXI if RDF is cleared */
  if (sh4->SCSCR2->RIE && !sh4->SCFSR2->RDF) {
    sh4_clear_interrupt(sh4, SH4_INT_SCIFRXI);
  }

  /* clear TXI if TDFE is cleared */
  if (sh4->SCSCR2->TIE && !sh4->SCFSR2->TDFE) {
    sh4_clear_interrupt(sh4, SH4_INT_SCIFTXI);
  }
}

REG_R32(sh4_cb, SCFRDR2) {
  struct sh4 *sh4 = dc->sh4;
  uint32_t data = receive_dequeue(sh4);
  return data;
}

REG_W32(sh4_cb, SCFRDR2) {
  LOG_FATAL("unexpected write to SCFRDR2");
}

REG_W32(sh4_cb, SCFCR2) {
  struct sh4 *sh4 = dc->sh4;
  sh4->SCFCR2->full = value;

  /* unsupported */
  CHECK_EQ(sh4->SCFCR2->LOOP, 0);

  /* reset fifos */
  if (sh4->SCFCR2->RFRST) {
    receive_reset(sh4);
  }
  if (sh4->SCFCR2->TFRST) {
    transmit_reset(sh4);
  }

  /* TODO handle MCE */

  /* unsupported */
  CHECK_EQ(sh4->SCFCR2->RSTRG, 0);
}

REG_R32(sh4_cb, SCLSR2) {
  struct sh4 *sh4 = dc->sh4;
  return sh4->SCLSR2->full;
}

REG_W32(sh4_cb, SCLSR2) {
  struct sh4 *sh4 = dc->sh4;

  /* TODO ORER can only be cleared if read as 1 first */

  sh4->SCLSR2->full = value;
}
