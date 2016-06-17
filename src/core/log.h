#ifndef REDREAM_LOG_H
#define REDREAM_LOG_H

#include <stdlib.h>
#include "core/debug_break.h"
#include "core/option.h"

DECLARE_OPTION_BOOL(debug);

enum log_level {
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARNING,
  LOG_LEVEL_FATAL,
};

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

void log_line(enum log_level level, const char *format, ...);

#define LOG_DEBUG(...)                        \
  if (OPTION_debug) {                         \
    log_line(LOG_LEVEL_DEBUG, ##__VA_ARGS__); \
  }

#define LOG_INFO(...)                        \
  do {                                       \
    log_line(LOG_LEVEL_INFO, ##__VA_ARGS__); \
  } while (0)

#define LOG_WARNING(...)                        \
  do {                                          \
    log_line(LOG_LEVEL_WARNING, ##__VA_ARGS__); \
  } while (0)

#ifndef NDEBUG
#define LOG_FATAL(...)                        \
  do {                                        \
    log_line(LOG_LEVEL_FATAL, ##__VA_ARGS__); \
    debug_break();                            \
    exit(1);                                  \
  } while (0)
#else
#define LOG_FATAL(...)                        \
  do {                                        \
    log_line(LOG_LEVEL_FATAL, ##__VA_ARGS__); \
    exit(1);                                  \
  } while (0)
#endif

#endif
