#include <stdio.h>
#include "system/system.h"
#include "emu/emulator.h"

using namespace dreavm;
using namespace dreavm::core;
using namespace dreavm::emu;
using namespace dreavm::system;

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

void RunEmulator(const char *launch) {
  System sys;
  Emulator emu(sys);

  if (!sys.Init()) {
    LOG(FATAL) << "Failed to initialize window.";
  }

  if (!emu.Init()) {
    LOG(FATAL) << "Failed to initialize emulator.";
  }

  if (launch && !emu.Launch(launch)) {
    LOG(FATAL) << "Failed to load " << launch;
  }

  while (1) {
    sys.Tick();
    emu.Tick();
  }
}

int main(int argc, char **argv) {
  // log to stderr by default
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);

  EnsureAppDirExists();

  InitFlags(&argc, &argv);

  RunEmulator(argc > 1 ? argv[1] : nullptr);

  ShutdownFlags();

  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
