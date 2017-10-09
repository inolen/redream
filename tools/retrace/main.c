#include "core/core.h"

extern int cmd_depth(int argc, const char **argv);

static void print_help() {
  LOG_INFO("usage: retrace <command> [<args> ...]");
  LOG_INFO("the available commands are:");
  LOG_INFO("    depth    compare depth function accuracies");
}

int main(int argc, const char **argv) {
  int res = 0;

  if (argc >= 2) {
    const char *cmd = argv[1];

    if (!strcmp(cmd, "depth")) {
      res = cmd_depth(argc - 2, argv + 2);
    }
  }

  if (!res) {
    print_help();
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
