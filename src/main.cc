#include <stdio.h>
#include <gflags/gflags.h>
#include "system/system.h"
#include "emu/emulator.h"

using namespace dreavm;
using namespace dreavm::core;
using namespace dreavm::emu;
using namespace dreavm::system;

DEFINE_string(gdi_file, "", "GD-ROM disc image to load");
DEFINE_string(bin_file, "", "Raw SH4 binary to load at 0x8c010000");

int main(int argc, char **argv) {
  // log to stderr by default
  FLAGS_logtostderr = true;

  google::InitGoogleLogging(argv[0]);
  google::ParseCommandLineFlags(&argc, &argv, true);

  System sys;
  Emulator emu(sys);

  if (!sys.Init()) {
    LOG(FATAL) << "Failed to initialize window.";
  }

  if (!emu.Init()) {
    LOG(FATAL) << "Failed to initialize emulator.";
  }

  // if (!FLAGS_gdi_file.empty()) {
  //   if (!emu.LaunchGDI(FLAGS_gdi_file.c_str())) {
  //     FATAL_ERROR("Failed to load %s\n", FLAGS_gdi_file.c_str());
  //   }
  // } else if (!FLAGS_bin_file.empty()) {
  //   if (!emu.LaunchBIN(FLAGS_bin_file.c_str())) {
  //     FATAL_ERROR("Failed to load %s\n", FLAGS_bin_file.c_str());
  //   }
  // } else {
  //   FATAL_ERROR("Must specify a bin or gdi file to load.\n");
  // }

  if (!emu.LaunchBIN("../dreamcast/tatest/tatest.bin")) {
    LOG(FATAL) << "Failed to load bin.";
  }

  if (!emu.LaunchGDI(
          "../dreamcast/Crazy Taxi 2 v1.004 (2001)(Sega)(NTSC)(US)[!]/Crazy Taxi 2 v1.004 (2001)(Sega)(NTSC)(US)[!].gdi")) {
    LOG(FATAL) << "Failed to load GDI.";
  }

  while (1) {
    sys.Tick();
    emu.Tick();
  }

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
