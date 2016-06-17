#include <stdarg.h>
#include <stdio.h>
#include "core/log.h"

DEFINE_OPTION_BOOL(debug, false, "Enable debug logging");

void log_line(enum log_level level, const char *format, ...) {
  static char sbuffer[0x1000];
  int buffer_size = sizeof(sbuffer);
  char *buffer = sbuffer;

  va_list args;
  // allocate a temporary buffer if need be to fit the string
  va_start(args, format);
  int len = vsnprintf(0, 0, format, args);
  if (len >= buffer_size) {
    buffer_size = len + 1;
    buffer = malloc(buffer_size);
  }
  va_end(args);

  va_start(args, format);
  vsnprintf(buffer, buffer_size, format, args);
  va_end(args);

#if defined(PLATFORM_LINUX) || defined(PLATFORM_DARWIN)
  switch (level) {
    case LOG_LEVEL_DEBUG:
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

  // cleanup the temporary buffer
  if (buffer != sbuffer) {
    free(buffer);
  }
}
