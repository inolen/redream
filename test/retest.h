#ifndef RETEST_H
#define RETEST_H

#include "core/assert.h"
#include "core/constructor.h"
#include "core/list.h"

typedef void (*test_callback_t)();

struct test {
  const char *name;
  test_callback_t run;
  struct list_node it;
};

#define TEST(name)                                 \
  static void name();                              \
  CONSTRUCTOR(TEST_REGISTER_##name) {              \
    static struct test test = {#name, &name, {0}}; \
    test_register(&test);                          \
  }                                                \
  void name()

void test_register(struct test *test);

#endif
