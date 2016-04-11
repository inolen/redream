#include <algorithm>
#include <gflags/gflags.h>
#include "emu/emulator.h"
#include "hw/aica/aica.h"
#include "hw/arm7/arm7.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/maple/maple.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "ui/window.h"

using namespace re;
using namespace re::emu;
using namespace re::hw;
using namespace re::hw::aica;
using namespace re::hw::arm7;
using namespace re::hw::gdrom;
using namespace re::hw::holly;
using namespace re::hw::maple;
using namespace re::hw::sh4;
using namespace re::renderer;
using namespace re::ui;

DEFINE_string(bios, "dc_boot.bin", "Path to BIOS");
DEFINE_string(flash, "dc_flash.bin", "Path to flash ROM");

Emulator::Emulator(ui::Window &window) : window_(window) {
  window_.AddListener(this);
}

Emulator::~Emulator() {
  window_.RemoveListener(this);

  DestroyDreamcast();
}

void Emulator::Run(const char *path) {
  if (!CreateDreamcast()) {
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

bool Emulator::CreateDreamcast() {
  dc_.sh4 = new SH4(dc_);
  dc_.arm7 = new ARM7(dc_);
  dc_.aica = new AICA(dc_);
  dc_.holly = new Holly(dc_);
  dc_.gdrom = new GDROM(dc_);
  dc_.maple = new Maple(dc_);
  dc_.pvr = new PVR2(dc_);
  dc_.ta = new TileAccelerator(dc_, window_.render_backend());

  if (!dc_.Init()) {
    DestroyDreamcast();
    return false;
  }

  return true;
}

void Emulator::DestroyDreamcast() {
  delete dc_.sh4;
  dc_.sh4 = nullptr;
  delete dc_.arm7;
  dc_.arm7 = nullptr;
  delete dc_.aica;
  dc_.aica = nullptr;
  delete dc_.holly;
  dc_.holly = nullptr;
  delete dc_.gdrom;
  dc_.gdrom = nullptr;
  delete dc_.maple;
  dc_.maple = nullptr;
  delete dc_.pvr;
  dc_.pvr = nullptr;
  delete dc_.ta;
  dc_.ta = nullptr;
}

bool Emulator::LoadBios(const char *path) {
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

  uint8_t *bios = dc_.memory->TranslateVirtual(BIOS_BEGIN);
  int n = static_cast<int>(fread(bios, sizeof(uint8_t), size, fp));
  fclose(fp);

  if (n != size) {
    LOG_WARNING("Bios read failed");
    return false;
  }

  return true;
}

bool Emulator::LoadFlash(const char *path) {
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

  uint8_t *flash = dc_.memory->TranslateVirtual(FLASH_BEGIN);
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
  uint32_t pc = 0x0c010000;
  uint8_t *data = dc_.memory->TranslateVirtual(pc);
  int n = static_cast<int>(fread(data, sizeof(uint8_t), size, fp));
  fclose(fp);

  if (n != size) {
    LOG_WARNING("BIN read failed");
    return false;
  }

  dc_.sh4->SetPC(pc);

  return true;
}

bool Emulator::LaunchGDI(const char *path) {
  std::unique_ptr<GDI> gdi(new GDI());

  if (!gdi->Load(path)) {
    return false;
  }

  dc_.gdrom->SetDisc(std::move(gdi));
  dc_.sh4->SetPC(0xa0000000);

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
