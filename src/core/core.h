#ifndef REDREAM_CORE_H
#define REDREAM_CORE_H

#include <stddef.h>
#include "core/math.h"

#define TYPEOF(n) __typeof__(n)

#define SWAP(a, b)       \
  do {                   \
    TYPEOF(a) tmp = (a); \
    (a) = (b);           \
    (b) = tmp;           \
  } while (0)

#define container_of(ptr, type, member)                \
  ({                                                   \
    const TYPEOF(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
  })

#define array_size(arr) (int)(sizeof(arr) / sizeof((arr)[0]))

#endif
