#ifndef DREAVM_ASSERT_H
#define DREAVM_ASSERT_H

#include "core/debug_break.h"
#include "core/log.h"
#include "core/platform.h"

namespace dvm {

// CHECK_* macros are usually true, hint this to the compiler if possible
#if defined(PLATFORM_LINUX) || defined(PLATFORM_DARWIN)
#define CHECK_EXPECT_TRUE(expr) (__builtin_expect(!!(expr), 1))
#else
#define CHECK_EXPECT_TRUE(expr) (expr)
#endif

const char *FormatCheckError(const char *filename, int linenum,
                             const char *expr, const char *format, ...);
const char *FormatCheckError(const char *filename, int linenum,
                             const char *expr);

// checks ran for all build configurations
#define CHECK_BINARY_OP(v1, v2, op, ...)                                      \
  do {                                                                        \
    if (!CHECK_EXPECT_TRUE((v1)op(v2))) {                                     \
      const char *msg = FormatCheckError(__FILE__, __LINE__,                  \
                                         #v1 " " #op " " #v2, ##__VA_ARGS__); \
      LOG_FATAL(msg);                                                         \
    }                                                                         \
  } while (0)
#define CHECK_EQ(v1, v2, ...) CHECK_BINARY_OP(v1, v2, ==, ##__VA_ARGS__)
#define CHECK_NE(v1, v2, ...) CHECK_BINARY_OP(v1, v2, !=, ##__VA_ARGS__)
#define CHECK_LE(v1, v2, ...) CHECK_BINARY_OP(v1, v2, <=, ##__VA_ARGS__)
#define CHECK_LT(v1, v2, ...) CHECK_BINARY_OP(v1, v2, <, ##__VA_ARGS__)
#define CHECK_GE(v1, v2, ...) CHECK_BINARY_OP(v1, v2, >=, ##__VA_ARGS__)
#define CHECK_GT(v1, v2, ...) CHECK_BINARY_OP(v1, v2, >, ##__VA_ARGS__)
#define CHECK_NOTNULL(val, ...)                                         \
  do {                                                                  \
    if (!CHECK_EXPECT_TRUE(val)) {                                      \
      const char *msg = FormatCheckError(                               \
          __FILE__, __LINE__, #val " must be non-NULL", ##__VA_ARGS__); \
      LOG_FATAL(msg);                                                   \
    }                                                                   \
  } while (0)
#define CHECK(condition, ...)                                              \
  do {                                                                     \
    if (!CHECK_EXPECT_TRUE(condition)) {                                   \
      const char *msg =                                                    \
          FormatCheckError(__FILE__, __LINE__, #condition, ##__VA_ARGS__); \
      LOG_FATAL(msg);                                                      \
    }                                                                      \
  } while (0)

// checks ran only for debug builds
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
}

#endif
