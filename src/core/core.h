#ifndef REDREAM_CORE_H
#define REDREAM_CORE_H

#include <stddef.h>
#include "core/math.h"

#ifdef __cplusplus

#include <type_traits>

#define TYPEOF(n) typename std::remove_reference<decltype(n)>::type

#else

#define TYPEOF(n) __typeof__(n)

#endif

#define SWAP(a, b)       \
  do {                   \
    TYPEOF(a) tmp = (a); \
    (a) = (b);           \
    (b) = tmp;           \
  } while (0)

#define container_of(ptr, type, member)               \
  ({                                                  \
    const TYPEOF(((type*)0)->member)* __mptr = (ptr); \
    (type*)((char*)__mptr - offsetof(type, member));  \
  })

#define array_size(arr) (sizeof(arr) / sizeof((arr)[0]))

#define array_resize(arr, new_size) \
  {                                 \
    array_reserve(arr, new_size);   \
    arr##_size = new_size;          \
  }

#define array_reserve(arr, new_capacity)                   \
  {                                                        \
    if (arr##_capacity < new_capacity) {                   \
      arr##_capacity = MAX(arr##_capacity, 4);             \
                                                           \
      while (arr##_capacity < new_capacity) {              \
        arr##_capacity *= 2;                               \
      }                                                    \
      arr = realloc(arr, arr##_capacity * sizeof(arr[0])); \
    }                                                      \
  }

#endif
