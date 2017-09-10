#ifndef REDREAM_TIME_H
#define REDREAM_TIME_H

#include <stdint.h>
#include <time.h>

#define NS_PER_SEC INT64_C(1000000000)
#define NS_PER_MS INT64_C(1000000)

int64_t time_nanoseconds();

#endif
