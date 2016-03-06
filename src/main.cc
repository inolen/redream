#include <stdio.h>
#include <memory>
#include <gflags/gflags.h>
#include "emu/emulator.h"
#include "emu/tracer.h"
#include "sys/exception_handler.h"
#include "sys/filesystem.h"
#include "sys/network.h"

using namespace re;
using namespace re::emu;
using namespace re::sys;

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

  if (!Network::Init()) {
    LOG_WARNING("Failed to initialize networking");
    return EXIT_FAILURE;
  }

  if (!ExceptionHandler::instance().Init()) {
    LOG_WARNING("Failed to initialize exception handler");
    return EXIT_FAILURE;
  }

  const char *load = argc > 1 ? argv[1] : nullptr;
  if (load && strstr(load, ".trace")) {
    std::unique_ptr<Tracer> tracer(new Tracer());
    tracer->Run(load);
  } else {
    std::unique_ptr<Emulator> emu(new Emulator());
    emu->Run(load);
  }

  Network::Shutdown();

  ShutdownFlags();

  return EXIT_SUCCESS;
}
