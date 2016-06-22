#include <mach/mach.h>
#include <mach/mach_time.h>
#include "sys/time.h"
#include "core/assert.h"

static const int64_t NS_PER_SEC = INT64_C(1000000000);

int64_t time_nanoseconds() {
  uint64_t result = mach_absolute_time();

  static mach_timebase_info_data_t timebase_info;
  if (timebase_info.denom == 0) {
    kern_return_t kr = mach_timebase_info(&timebase_info);
    DCHECK_EQ(kr, KERN_SUCCESS);
  }

  return (int64_t)(result * timebase_info.numer / timebase_info.denom);
}
