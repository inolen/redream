#include <stdarg.h>
#include <stdio.h>
#include "core/assert.h"

#define MAX_ERROR_SIZE 1024

const char *format_check_error_ex(const char *filename, int linenum,
                                  const char *expr, const char *unused,
                                  const char *format, ...) {
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

const char *format_check_error(const char *filename, int linenum,
                               const char *expr, const char *unused) {
  return format_check_error_ex(filename, linenum, expr, unused, NULL);
}
