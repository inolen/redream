#include <stdlib.h>
#include "retest.h"
#include "core/option.h"
#include "sys/filesystem.h"

static struct list tests;

void test_register(struct test *test) {
  list_add(&tests, &test->it);
}

int main(int argc, char **argv) {
  const char *appdir = fs_appdir();
  if (!fs_mkdir(appdir)) {
    LOG_FATAL("Failed to create app directory %s", appdir);
  }

  /* load base options from config */
  char config[PATH_MAX] = {0};
  snprintf(config, sizeof(config), "%s" PATH_SEPARATOR "config", appdir);
  options_read(config);

  /* override options from the command line */
  options_parse(&argc, &argv);

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
