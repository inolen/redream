#include <stdio.h>
#include "system/system.h"
#include "emu/emulator.h"
#include "trace/trace_viewer.h"

using namespace dreavm::core;
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

void RunTraceViewer(const char *trace) {
  System sys;
  TraceViewer tracer(sys);

  if (!sys.Init()) {
    LOG(FATAL) << "Failed to initialize window.";
  }

  if (!tracer.Init()) {
    LOG(FATAL) << "Failed to initialize tracer.";
  }

  if (!tracer.Load(trace)) {
    LOG(FATAL) << "Failed to load " << trace;
  }

  while (1) {
    sys.Tick();
    tracer.Tick();
  }
}

int main(int argc, char **argv) {
  // log to stderr by default
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);

  EnsureAppDirExists();

  InitFlags(&argc, &argv);

  const char *load = argc > 1 ? argv[1] : nullptr;
  if (load && strstr(load, ".trace")) {
    RunTraceViewer(load);
  } else {
    RunEmulator(load);
  }

  ShutdownFlags();

  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
