#ifndef REDREAM_MEMORY_H
#define REDREAM_MEMORY_H

#include <string.h>

#if PLATFORM_WINDOWS
#define alloca _alloca
#endif

template <typename T>
T load(const void *ptr) {
  return *reinterpret_cast<const T *>(ptr);
}

template <typename T>
void store(void *ptr, T v) {
  *reinterpret_cast<T *>(ptr) = v;
}

#endif
