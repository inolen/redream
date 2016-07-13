#ifndef REDREAM_CORE_H
#define REDREAM_CORE_H

#include <stddef.h>
#include <stdint.h>
#include "core/math.h"

#define array_size(arr) (int)(sizeof(arr) / sizeof((arr)[0]))

#if PLATFORM_WINDOWS

static inline void *container_of_(void *ptr, ptrdiff_t offset) {
  return (char *)ptr - offset;
}

static inline void *container_of_safe_(void *ptr, ptrdiff_t offset) {
  return ptr ? (char *)ptr - offset : NULL;
}

#define container_of(ptr, type, member) \
  ((type *)container_of_((void *)ptr, offsetof(type, member)))

#define container_of_safe(ptr, type, member) \
  ((type *)container_of_safe_((void *)ptr, offsetof(type, member)))

#else

#define container_of(ptr, type, member)                \
  ({                                                   \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
  })

#define container_of_safe(ptr, type, member)                        \
  ({                                                                \
    const typeof(((type *)0)->member) *__mptr = (ptr);              \
    ptr ? (type *)((char *)__mptr - offsetof(type, member)) : NULL; \
  })

#endif

#endif
