#include <stdarg.h>
#include <stdio.h>
#include "core/assert.h"

const char *FormatCheckError(const char *filename, int linenum,
                             const char *expr, const char *format, ...) {
  static const int MAX_ERROR_SIZE = 1024;
  static char error[MAX_ERROR_SIZE];
  static char custom[MAX_ERROR_SIZE];

  if (format) {
    va_list args;
    va_start(args, format);
    vsnprintf(custom, sizeof(custom), format, args);
    va_end(args);

    snprintf(error, sizeof(error), "[%s:%d] Check failed: %s\n[%s:%d] %s\n",
             filename, linenum, expr, filename, linenum, custom);
  } else {
    snprintf(error, sizeof(error), "[%s:%d] Check failed: %s\n", filename,
             linenum, expr);
  }

  return error;
}

const char *FormatCheckError(const char *filename, int linenum,
                             const char *expr) {
  return FormatCheckError(filename, linenum, expr, nullptr);
}
