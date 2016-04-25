#include <algorithm>
#include <gflags/gflags.h>
#include "emu/emulator.h"
#include "hw/gdrom/gdrom.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "ui/window.h"

using namespace re;
using namespace re::emu;
using namespace re::hw;
using namespace re::hw::gdrom;
using namespace re::ui;

DEFINE_string(bios, "dc_boot.bin", "Path to BIOS");
DEFINE_string(flash, "dc_flash.bin", "Path to flash ROM");

Emulator::Emulator(Window &window)
    : window_(window), dc_(window_.render_backend()) {
  window_.AddListener(this);
}

Emulator::~Emulator() { window_.RemoveListener(this); }

void Emulator::Run(const char *path) {
  if (!dc_.Init()) {
    return;
  }

  if (!LoadBios(FLAGS_bios.c_str())) {
    return;
  }

  if (!LoadFlash(FLAGS_flash.c_str())) {
    return;
  }

  if (path) {
    LOG_INFO("Launching %s", path);

    if ((strstr(path, ".bin") && !LaunchBIN(path)) ||
        (strstr(path, ".gdi") && !LaunchGDI(path))) {
      LOG_WARNING("Failed to launch %s", path);
      return;
    }
  }

  // start running
  static const std::chrono::nanoseconds MACHINE_STEP = HZ_TO_NANO(1000);
  static const std::chrono::nanoseconds FRAME_STEP = HZ_TO_NANO(60);

  auto current_time = std::chrono::high_resolution_clock::now();
  auto last_time = current_time;

  auto next_machine_time = current_time;
  auto next_frame_time = current_time;

  running_ = true;

  while (running_) {
    current_time = std::chrono::high_resolution_clock::now();
    last_time = current_time;

    // run dreamcast machine
    if (current_time > next_machine_time) {
      dc_.Tick(MACHINE_STEP);

      next_machine_time = current_time + MACHINE_STEP;
    }

    // run local frame
    if (current_time > next_frame_time) {
      window_.PumpEvents();

      next_frame_time = current_time + FRAME_STEP;
    }
  }
}

bool Emulator::LoadBios(const char *path) {
  static const int BIOS_BEGIN = 0x00000000;
  static const int BIOS_SIZE = 0x00200000;

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    LOG_WARNING("Failed to open bios at \"%s\"", path);
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != BIOS_SIZE) {
    LOG_WARNING("Bios size mismatch, is %d, expected %d", size, BIOS_SIZE);
    fclose(fp);
    return false;
  }

  uint8_t *bios = dc_.sh4()->space().Translate(BIOS_BEGIN);
  int n = static_cast<int>(fread(bios, sizeof(uint8_t), size, fp));
  fclose(fp);

  if (n != size) {
    LOG_WARNING("Bios read failed");
    return false;
  }

  return true;
}

bool Emulator::LoadFlash(const char *path) {
  static const int FLASH_BEGIN = 0x00200000;
  static const int FLASH_SIZE = 0x00020000;

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    LOG_WARNING("Failed to open flash at \"%s\"", path);
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != FLASH_SIZE) {
    LOG_WARNING("Flash size mismatch, is %d, expected %d", size, FLASH_SIZE);
    fclose(fp);
    return false;
  }

  uint8_t *flash = dc_.sh4()->space().Translate(FLASH_BEGIN);
  int n = static_cast<int>(fread(flash, sizeof(uint8_t), size, fp));
  fclose(fp);

  if (n != size) {
    LOG_WARNING("Flash read failed");
    return false;
  }

  return true;
}

bool Emulator::LaunchBIN(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // load to 0x0c010000 (area 3) which is where 1ST_READ.BIN is loaded to
  uint8_t *data = dc_.sh4()->space().Translate(0x0c010000);
  int n = static_cast<int>(fread(data, sizeof(uint8_t), size, fp));
  fclose(fp);

  if (n != size) {
    LOG_WARNING("BIN read failed");
    return false;
  }

  dc_.gdrom()->SetDisc(nullptr);
  dc_.sh4()->SetPC(0x0c010000);

  return true;
}

bool Emulator::LaunchGDI(const char *path) {
  std::unique_ptr<GDI> gdi(new GDI());

  if (!gdi->Load(path)) {
    return false;
  }

  dc_.gdrom()->SetDisc(std::move(gdi));
  dc_.sh4()->SetPC(0xa0000000);

  return true;
}

void Emulator::OnPaint(bool show_main_menu) { dc_.OnPaint(show_main_menu); }

void Emulator::OnKeyDown(Keycode code, int16_t value) {
  if (code == K_F1) {
    if (value) {
      window_.EnableMainMenu(!window_.MainMenuEnabled());
    }
    return;
  }

  dc_.OnKeyDown(code, value);
}

void Emulator::OnClose() { running_ = false; }
