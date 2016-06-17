#include "core/log.h"
#include "core/option.h"
#include "emu/emulator.h"
#include "emu/tracer.h"
#include "sys/exception_handler.h"
#include "sys/filesystem.h"
#include "ui/window.h"

DEFINE_OPTION_BOOL(help, false, "Show help");

// void InitFlags(int *argc, char ***argv) {
//   const char *appdir = fs_appdir();

//   char flagfile[PATH_MAX] = {};
//   snprintf(flagfile, sizeof(flagfile), "%s" PATH_SEPARATOR "flags", appdir);

//   // read any saved flags
//   if (fs_exists(flagfile)) {
//     google::ReadFromFlagsFile(flagfile, nullptr, false);
//   }

//   // parse new flags from the command line
//   google::ParseCommandLineFlags(argc, argv, true);

//   // update saved flags
//   remove(flagfile);
//   google::AppendFlagsIntoFile(flagfile, nullptr);
// }

// void ShutdownFlags() {
//   google::ShutDownCommandLineFlags();
// }

int main(int argc, char **argv) {
  const char *appdir = fs_appdir();

  if (!fs_mkdir(appdir)) {
    LOG_FATAL("Failed to create app directory %s", appdir);
  }

  option_parse(&argc, &argv);

  if (OPTION_help) {
    option_print_help();
    return EXIT_SUCCESS;
  }

  // InitFlags(&argc, &argv);

  if (!exception_handler_install()) {
    LOG_WARNING("Failed to initialize exception handler");
    return EXIT_FAILURE;
  }

  struct window *window = win_create();
  if (!window) {
    LOG_WARNING("Failed to initialize window");
    return EXIT_FAILURE;
  }

  const char *load = argc > 1 ? argv[1] : NULL;
  if (load && strstr(load, ".trace")) {
    // struct tracer_s *tracer = tracer_create(window);
    // tracer_run(tracer, load);
    // tracer_destroy(tracer);
  } else {
    struct emu_s *emu = emu_create(window);
    emu_run(emu, load);
    emu_destroy(emu);
  }

  win_destroy(window);

  exception_handler_uninstall();

  // ShutdownFlags();

  return EXIT_SUCCESS;
}
