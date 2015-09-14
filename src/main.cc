#include <stdio.h>
#include "core/core.h"
#include "emu/emulator.h"
#include "trace/trace_viewer.h"

using namespace dreavm;
using namespace dreavm::emu;
using namespace dreavm::system;
using namespace dreavm::trace;

void InitFlags(int *argc, char ***argv) {
  const char *appdir = GetAppDir();

  char flagfile[PATH_MAX] = {};
  snprintf(flagfile, sizeof(flagfile), "%s" PATH_SEPARATOR "flags", appdir);

  // read any saved flags
  if (Exists(flagfile)) {
    google::ReadFromFlagsFile(flagfile, nullptr, false);
  }

  // parse new flags from the command line
  google::ParseCommandLineFlags(argc, argv, true);

  // update saved flags
  remove(flagfile);
  google::AppendFlagsIntoFile(flagfile, nullptr);
}

void ShutdownFlags() { google::ShutDownCommandLineFlags(); }

int main(int argc, char **argv) {
  EnsureAppDirExists();

  InitFlags(&argc, &argv);

  const char *load = argc > 1 ? argv[1] : nullptr;
  if (load && strstr(load, ".trace")) {
    TraceViewer tracer;
    tracer.Run(load);
  } else {
    Emulator emu;
    emu.Run(load);
  }

  ShutdownFlags();

  return EXIT_SUCCESS;
}
