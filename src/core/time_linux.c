#include <time.h>
#include "core/time.h"

int64_t time_nanoseconds() {
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  return (int64_t)tp.tv_sec * NS_PER_SEC + (int64_t)tp.tv_nsec;
}
