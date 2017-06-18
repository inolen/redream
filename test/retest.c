#include <stdlib.h>
#include "retest.h"
#include "core/filesystem.h"
#include "core/option.h"

static struct list tests;

void test_register(struct test *test) {
  list_add(&tests, &test->it);
}

int main(int argc, char **argv) {
  /* set application directory */
  char appdir[PATH_MAX];
  char userdir[PATH_MAX];
  int r = fs_userdir(userdir, sizeof(userdir));
  CHECK(r);
  snprintf(appdir, sizeof(appdir), "%s" PATH_SEPARATOR ".redream", userdir);
  fs_set_appdir(appdir);

  list_for_each_entry(test, &tests, struct test, it) {
    LOG_INFO("===-----------------------------------------------------===");
    LOG_INFO("%s", test->name);
    LOG_INFO("===-----------------------------------------------------===");
    test->run();
    LOG_INFO(ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET);
    LOG_INFO("");
  }

  return EXIT_SUCCESS;
}
