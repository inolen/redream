#include <stdarg.h>
#include <stdio.h>
#include "core/log.h"
#include "core/platform.h"

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

void Log(LogLevel level, const char *format, ...) {
  static char buffer[1024];

  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

#if defined(PLATFORM_LINUX) || defined(PLATFORM_DARWIN)
  switch (level) {
    case LOG_LEVEL_INFO:
      printf("%s\n", buffer);
      break;
    case LOG_LEVEL_WARNING:
      printf(ANSI_COLOR_YELLOW "%s" ANSI_COLOR_RESET "\n", buffer);
      break;
    case LOG_LEVEL_FATAL:
      printf(ANSI_COLOR_RED "%s" ANSI_COLOR_RESET "\n", buffer);
      break;
  }
#else
  printf("%s\n", buffer);
#endif
}
