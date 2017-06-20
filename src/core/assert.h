#ifndef REDREAM_ASSERT_H
#define REDREAM_ASSERT_H

#include "core/log.h"

/* CHECK_* macros are usually true, hint this to the compiler if possible */
#if PLATFORM_ANDROID || PLATFORM_DARWIN || PLATFORM_LINUX
#define CHECK_EXPECT_TRUE(expr) (__builtin_expect(!!(expr), 1))
#else
#define CHECK_EXPECT_TRUE(expr) (expr)
#endif

const char *format_check_error_ex(const char *filename, int linenum,
                                  const char *expr, const char *unused,
                                  const char *format, ...);
const char *format_check_error(const char *filename, int linenum,
                               const char *expr, const char *unused);

/* 3: VA_ARGS = format_check_error */
/* 4+: VA_ARGS = format_check_error_ex */
#define SELECT_FORMAT_CHECK_ERROR(_1, _2, _3, _4, _5, _6, NAME, ...) NAME
#define EXPAND_FORMAT_CHECK_ERROR(x) x

#define FORMAT_CHECK_ERROR(filename, linenum, expr, ...)                       \
  EXPAND_FORMAT_CHECK_ERROR(SELECT_FORMAT_CHECK_ERROR(                         \
      __VA_ARGS__, /* unused, */ format_check_error_ex, format_check_error_ex, \
      format_check_error_ex, format_check_error_ex, format_check_error_ex,     \
      format_check_error)(filename, linenum, expr, ##__VA_ARGS__))

/* checks ran for all build configurations */
#define CHECK_BINARY_OP(v1, v2, op, ...)                              \
  do {                                                                \
    if (!CHECK_EXPECT_TRUE((v1)op(v2))) {                             \
      const char *msg = FORMAT_CHECK_ERROR(                           \
          __FILE__, __LINE__, #v1 " " #op " " #v2, 0, ##__VA_ARGS__); \
      LOG_FATAL(msg);                                                 \
    }                                                                 \
  } while (0)
#define CHECK_EQ(v1, v2, ...) CHECK_BINARY_OP(v1, v2, ==, ##__VA_ARGS__)
#define CHECK_NE(v1, v2, ...) CHECK_BINARY_OP(v1, v2, !=, ##__VA_ARGS__)
#define CHECK_LE(v1, v2, ...) CHECK_BINARY_OP(v1, v2, <=, ##__VA_ARGS__)
#define CHECK_LT(v1, v2, ...) CHECK_BINARY_OP(v1, v2, <, ##__VA_ARGS__)
#define CHECK_GE(v1, v2, ...) CHECK_BINARY_OP(v1, v2, >=, ##__VA_ARGS__)
#define CHECK_GT(v1, v2, ...) CHECK_BINARY_OP(v1, v2, >, ##__VA_ARGS__)
#define CHECK_NOTNULL(val, ...)                                            \
  do {                                                                     \
    if (!CHECK_EXPECT_TRUE(val)) {                                         \
      const char *msg = FORMAT_CHECK_ERROR(                                \
          __FILE__, __LINE__, #val " must be non-NULL", 0, ##__VA_ARGS__); \
      LOG_FATAL(msg);                                                      \
    }                                                                      \
  } while (0)
#define CHECK_STREQ(v1, v2, ...)                                       \
  do {                                                                 \
    if (!CHECK_EXPECT_TRUE(!strcmp(v1, v2))) {                         \
      const char *msg = FORMAT_CHECK_ERROR(                            \
          __FILE__, __LINE__, "expected '" #v1 "' to eq '" #v2 "'", 0, \
          ##__VA_ARGS__);                                              \
      LOG_FATAL(msg);                                                  \
    }                                                                  \
  } while (0)

#define CHECK(condition, ...)                                                 \
  do {                                                                        \
    if (!CHECK_EXPECT_TRUE(condition)) {                                      \
      const char *msg = FORMAT_CHECK_ERROR(__FILE__, __LINE__, #condition, 0, \
                                           ##__VA_ARGS__);                    \
      LOG_FATAL(msg);                                                         \
    }                                                                         \
  } while (0)

/* checks ran only for debug builds */
#ifndef NDEBUG
#define DCHECK_EQ(v1, v2, ...) CHECK_EQ(v1, v2, ##__VA_ARGS__)
#define DCHECK_NE(v1, v2, ...) CHECK_NE(v1, v2, ##__VA_ARGS__)
#define DCHECK_LE(v1, v2, ...) CHECK_LE(v1, v2, ##__VA_ARGS__)
#define DCHECK_LT(v1, v2, ...) CHECK_LT(v1, v2, ##__VA_ARGS__)
#define DCHECK_GE(v1, v2, ...) CHECK_GE(v1, v2, ##__VA_ARGS__)
#define DCHECK_GT(v1, v2, ...) CHECK_GT(v1, v2, ##__VA_ARGS__)
#define DCHECK_NOTNULL(val, ...) CHECK_NOTNULL(v1, v2, ##__VA_ARGS__)
#define DCHECK(condition, ...) CHECK(condition, ##__VA_ARGS__)
#else
#define DCHECK_EQ(v1, v2, ...)
#define DCHECK_NE(v1, v2, ...)
#define DCHECK_LE(v1, v2, ...)
#define DCHECK_LT(v1, v2, ...)
#define DCHECK_GE(v1, v2, ...)
#define DCHECK_GT(v1, v2, ...)
#define DCHECK_NOTNULL(val, ...)
#define DCHECK(condition, ...)
#endif

#endif
