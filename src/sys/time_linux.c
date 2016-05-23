#include <time.h>
#include "sys/time.h"

static const int64_t NS_PER_SEC = INT64_C(1000000000);

int64_t time_nanoseconds() {
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  return (int64_t)tp.tv_sec * NS_PER_SEC + (int64_t)tp.tv_nsec;
}
