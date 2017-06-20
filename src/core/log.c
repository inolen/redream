#include <stdarg.h>
#include <stdio.h>
#include "core/log.h"

#if PLATFORM_ANDROID
#include <android/log.h>
#endif

void log_line(enum log_level level, const char *format, ...) {
  static char sbuffer[0x1000];
  int buffer_size = sizeof(sbuffer);
  char *buffer = sbuffer;

  /* allocate a temporary buffer if need be to fit the string */
  va_list args;
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

#if PLATFORM_ANDROID
  static const char *LOG_TAG = "redream";

  switch (level) {
    case LOG_LEVEL_INFO:
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s\n", buffer);
      break;
    case LOG_LEVEL_WARNING:
      __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "%s\n", buffer);
      break;
    case LOG_LEVEL_FATAL:
      __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, "%s\n", buffer);
      break;
  }
#elif PLATFORM_DARWIN || PLATFORM_LINUX
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

  /* cleanup the temporary buffer */
  if (buffer != sbuffer) {
    free(buffer);
  }
}
