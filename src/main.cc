#include <stdio.h>
#include <gflags/gflags.h>
#include "system/system.h"
#include "emu/emulator.h"

using namespace dreavm;
using namespace dreavm::core;
using namespace dreavm::emu;
using namespace dreavm::system;

const char *GetAppDir() {
  static char appdir[PATH_MAX] = {};

  if (appdir[0]) {
    return appdir;
  }

  // get the user's home directory
  char userdir[PATH_MAX];
  if (!getuserdir(userdir, sizeof(userdir))) {
    return nullptr;
  }

  // setup our own subdirectory inside of it
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s" PATH_SEPARATOR ".dreavm", userdir);

  // ensure the subdirectory actually exists
  if (!mkdir(tmp)) {
    return nullptr;
  }

  strncpy(appdir, tmp, sizeof(appdir));

  return appdir;
}

void InitFlags(int *argc, char ***argv) {
  // get the flag file path
  char flagfile[PATH_MAX] = {};

  const char *appdir = GetAppDir();
  if (appdir) {
    snprintf(flagfile, sizeof(flagfile), "%s" PATH_SEPARATOR "flags", appdir);
  }

  // read any saved flags
  if (flagfile[0]) {
    if (exists(flagfile)) {
      google::ReadFromFlagsFile(flagfile, nullptr, false);
    }
  }

  // parse new flags from the command line
  google::ParseCommandLineFlags(argc, argv, true);

  // update saved flags
  if (flagfile[0]) {
    remove(flagfile);

    google::AppendFlagsIntoFile(flagfile, nullptr);
  }
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

  InitFlags(&argc, &argv);

  RunEmulator(argc > 1 ? argv[1] : nullptr);

  ShutdownFlags();

  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
