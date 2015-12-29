#ifndef DREAVM_LOG_H
#define DREAVM_LOG_H

#include <stdlib.h>

enum LogLevel { LOG_LEVEL_INFO, LOG_LEVEL_WARNING, LOG_LEVEL_FATAL };

void Log(LogLevel level, const char *format, ...);

#define LOG_INFO(...)                   \
  do {                                  \
    Log(LOG_LEVEL_INFO, ##__VA_ARGS__); \
  } while (0)

#define LOG_WARNING(...)                   \
  do {                                     \
    Log(LOG_LEVEL_WARNING, ##__VA_ARGS__); \
  } while (0)

#ifndef NDEBUG
#define LOG_FATAL(...)                   \
  do {                                   \
    Log(LOG_LEVEL_FATAL, ##__VA_ARGS__); \
    debug_break();                       \
    exit(1);                             \
  } while (0)
#else
#define LOG_FATAL(...)                   \
  do {                                   \
    Log(LOG_LEVEL_FATAL, ##__VA_ARGS__); \
    exit(1);                             \
  } while (0)
#endif

#endif
