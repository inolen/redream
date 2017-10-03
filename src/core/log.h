#ifndef REDREAM_LOG_H
#define REDREAM_LOG_H

#include <stdio.h>
#include <stdlib.h>

enum log_level {
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

#ifndef NDEBUG
#if COMPILER_MSVC
#define DEBUGBREAK() __debugbreak()
#else
#define DEBUGBREAK() __builtin_trap()
#endif
#else
#define DEBUGBREAK()
#endif

#define LOG_INFO(...)                        \
  do {                                       \
    log_line(LOG_LEVEL_INFO, ##__VA_ARGS__); \
  } while (0)

#define LOG_WARNING(...)                        \
  do {                                          \
    log_line(LOG_LEVEL_WARNING, ##__VA_ARGS__); \
  } while (0)

#define LOG_FATAL(...)                        \
  do {                                        \
    log_line(LOG_LEVEL_FATAL, ##__VA_ARGS__); \
    fflush(stdout);                           \
    DEBUGBREAK();                             \
    exit(1);                                  \
  } while (0)

#endif
