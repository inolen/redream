#include <thread>
#include <gflags/gflags.h>
#include "emu/emulator.h"
#include "emu/profiler.h"
#include "hw/aica/aica.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/tile_renderer.h"
#include "hw/holly/trace.h"
#include "hw/maple/maple.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "renderer/gl_backend.h"

using namespace re;
using namespace re::emu;
using namespace re::hw;
using namespace re::hw::aica;
using namespace re::hw::gdrom;
using namespace re::hw::holly;
using namespace re::hw::maple;
using namespace re::hw::sh4;
using namespace re::renderer;
using namespace re::sys;

DEFINE_string(bios, "dc_boot.bin", "Path to BIOS");
DEFINE_string(flash, "dc_flash.bin", "Path to flash ROM");

Emulator::Emulator() : rb_(nullptr), tile_renderer_(nullptr), speed_() {}

Emulator::~Emulator() {
  delete rb_;
  delete tile_renderer_;

  DestroyDreamcast();
}

void Emulator::Run(const char *path) {
  if (!window_.Init()) {
    return;
  }

  // initialize renderer backend
  rb_ = new GLBackend(window_);

  if (!rb_->Init()) {
    return;
  }

  // initialize dreamcast machine and all dependent hardware
  if (!CreateDreamcast()) {
    return;
  }

  // setup tile renderer with the renderer backend and the dreamcast's
  // internal textue cache
  tile_renderer_ = new TileRenderer(*rb_, *dc_.ta);

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
  static const std::chrono::nanoseconds SAMPLE_PERIOD = HZ_TO_NANO(10);

  auto current_time = std::chrono::high_resolution_clock::now();
  auto last_time = current_time;

  auto next_machine_time = current_time;
  auto next_frame_time = current_time;
  auto next_sample_time = current_time;

  auto host_time = std::chrono::nanoseconds(0);
  auto guest_time = std::chrono::nanoseconds(0);

  running_ = true;

  while (running_) {
    current_time = std::chrono::high_resolution_clock::now();
    last_time = current_time;

    // run machine
    if (current_time > next_machine_time) {
      dc_.Tick(MACHINE_STEP);

      host_time += std::chrono::high_resolution_clock::now() - last_time;
      guest_time += MACHINE_STEP;
      next_machine_time = current_time + MACHINE_STEP;
    }

    // pump input / render frame
    if (current_time > next_frame_time) {
      PumpEvents();
      RenderFrame();

      next_frame_time = current_time + FRAME_STEP;
    }

    // update debug stats
    if (current_time > next_sample_time) {
      float speed =
          (guest_time.count() / static_cast<float>(host_time.count())) * 100.0f;
      speed_ = *reinterpret_cast<uint32_t *>(&speed);

      host_time = std::chrono::nanoseconds(0);
      guest_time = std::chrono::nanoseconds(0);
      next_sample_time = current_time + SAMPLE_PERIOD;
    }
  }
}

bool Emulator::CreateDreamcast() {
  dc_.sh4 = new SH4(&dc_);
  dc_.aica = new AICA(&dc_);
  dc_.gdrom = new GDROM(&dc_);
  dc_.holly = new Holly(&dc_);
  dc_.maple = new Maple(&dc_);
  dc_.pvr = new PVR2(&dc_);
  dc_.ta = new TileAccelerator(&dc_, rb_);

  if (!dc_.Init()) {
    DestroyDreamcast();
    return false;
  }

  return true;
}

void Emulator::DestroyDreamcast() {
  delete dc_.sh4;
  dc_.sh4 = nullptr;
  delete dc_.aica;
  dc_.aica = nullptr;
  delete dc_.gdrom;
  dc_.gdrom = nullptr;
  delete dc_.holly;
  dc_.holly = nullptr;
  delete dc_.maple;
  dc_.maple = nullptr;
  delete dc_.pvr;
  dc_.pvr = nullptr;
  delete dc_.ta;
  dc_.ta = nullptr;
  delete dc_.trace_writer;
  dc_.trace_writer = nullptr;
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

  uint8_t *bios = dc_.memory->TranslateVirtual(BIOS_START);
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

  uint8_t *flash = dc_.memory->TranslateVirtual(FLASH_START);
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

void Emulator::ToggleTracing() {
  if (!dc_.trace_writer) {
    char filename[PATH_MAX];
    GetNextTraceFilename(filename, sizeof(filename));

    dc_.trace_writer = new TraceWriter();

    if (!dc_.trace_writer->Open(filename)) {
      delete dc_.trace_writer;
      dc_.trace_writer = nullptr;

      LOG_INFO("Failed to start tracing");

      return;
    }

    // clear texture cache in order to generate insert events for all textures
    // referenced while tracing
    dc_.ta->ClearTextures();

    LOG_INFO("Begin tracing to %s", filename);
  } else {
    delete dc_.trace_writer;
    dc_.trace_writer = nullptr;

    LOG_INFO("End tracing");
  }
}

void Emulator::RenderFrame() {
  rb_->BeginFrame();

  // render the latest tile context
  if (TileContext *tactx = dc_.ta->GetLastContext()) {
    tile_renderer_->RenderContext(tactx);
  }

  // render stats
  char stats[512];
  float speed = *reinterpret_cast<float *>(&speed_);
  snprintf(stats, sizeof(stats), "%.2f%%, %.2f rps", speed, dc_.pvr->rps());
  rb_->RenderText2D(0, 0, 12.0f, 0xffffffff, stats);

  // render profiler
  profiler_.Render(rb_);

  rb_->EndFrame();
}

void Emulator::PumpEvents() {
  WindowEvent ev;

  window_.PumpEvents();

  while (window_.PollEvent(&ev)) {
    switch (ev.type) {
      case WE_KEY: {
        // let the profiler take a stab at the input first
        if (!profiler_.HandleInput(ev.key.code, ev.key.value)) {
          if (ev.key.code == K_F2) {
            if (ev.key.value) {
              ToggleTracing();
            }
          } else {
            dc_.maple->HandleInput(0, ev.key.code, ev.key.value);
          }
        }
      } break;

      case WE_MOUSEMOVE: {
        profiler_.HandleMouseMove(ev.mousemove.x, ev.mousemove.y);
      } break;

      case WE_RESIZE: {
        rb_->ResizeVideo(ev.resize.width, ev.resize.height);
      } break;

      case WE_QUIT: {
        running_ = false;
      } break;
    }
  }
}
