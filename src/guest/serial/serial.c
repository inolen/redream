#include "guest/serial/serial.h"
#include "guest/dreamcast.h"

struct serial {
  struct device;
  void *userdata;
  getchar_cb getchar;
  putchar_cb putchar;
};

static int serial_init(struct device *dev) {
  struct serial *serial = (struct serial *)dev;
  return 1;
}

void serial_putchar(struct serial *serial, int c) {
  serial->putchar(serial->userdata, c);
}

int serial_getchar(struct serial *serial) {
  return serial->getchar(serial->userdata);
}

void serial_destroy(struct serial *serial) {
  dc_destroy_device((struct device *)serial);
}

struct serial *serial_create(struct dreamcast *dc, void *userdata,
                             getchar_cb getchar, putchar_cb putchar) {
  struct serial *serial =
      dc_create_device(dc, sizeof(struct serial), "serial", &serial_init, NULL);
  serial->userdata = userdata;
  serial->getchar = getchar;
  serial->putchar = putchar;
  return serial;
}
