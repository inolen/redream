#include <stdlib.h>
#include "retest.h"

static struct list tests;

void test_register(struct test *test) {
  list_add(&tests, &test->it);
}

int main() {
  list_for_each_entry(test, &tests, struct test, it) {
    LOG_INFO("%s..", test->name);
    test->run();
    LOG_INFO(ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET);
  }

  return EXIT_SUCCESS;
}
