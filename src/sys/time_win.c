#include <Windows.h>
#include "sys/time.h"
#include "core/assert.h"

static const int64_t NS_PER_SEC = INT64_C(1000000000);

int64_t time_nanoseconds() {
  static double scale = 0.0;

  if (!scale) {
    LARGE_INTEGER freq;
    CHECK(QueryPerformanceFrequency(&freq));
    scale = (double)freq.QuadPart / NS_PER_SEC;
  }

  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);

  return (int64_t)((double)counter.QuadPart / scale);
}
