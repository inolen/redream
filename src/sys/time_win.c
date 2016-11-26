#include <Windows.h>
#include "sys/time.h"
#include "core/assert.h"

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
