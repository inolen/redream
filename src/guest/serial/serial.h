#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

struct dreamcast;

typedef int (*getchar_cb)(void *);
typedef void (*putchar_cb)(void *, int);

struct serial *serial_create(struct dreamcast *dc, void *userdata,
                             getchar_cb getchar, putchar_cb putchar);
void serial_destroy(struct serial *serial);

int serial_getchar(struct serial *serial);
void serial_putchar(struct serial *serial, int c);

#endif
