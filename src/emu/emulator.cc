#include "core/core.h"
#include "emu/emulator.h"
#include "emu/profiler.h"
#include "hw/dreamcast.h"
#include "renderer/gl_backend.h"

using namespace dreavm;
using namespace dreavm::emu;
using namespace dreavm::hw;
using namespace dreavm::hw::gdrom;
using namespace dreavm::hw::holly;
using namespace dreavm::renderer;
using namespace dreavm::sys;
using namespace dreavm::trace;

// scheduler is ticked at 1000hz, this is fairly arbitrary, but seems to be a
// good balance of executing cycles / handling interrupts
static const std::chrono::nanoseconds SCHEDULER_STEP = HZ_TO_NANO(1000);

// process input and render frames at 60hz
static const std::chrono::nanoseconds FRAME_STEP = HZ_TO_NANO(60);

DEFINE_string(bios, "dc_bios.bin", "Path to BIOS");
DEFINE_string(flash, "dc_flash.bin", "Path to flash ROM");

Emulator::Emulator() : trace_writer_(nullptr), deltas_(), delta_seq_(0) {
  rb_ = new GLBackend(wnd_);
  dc_.set_rb(rb_);
}

Emulator::~Emulator() {
  delete rb_;
  delete trace_writer_;
}

void Emulator::Run(const char *path) {
  if (!wnd_.Init()) {
    return;
  }

  if (!rb_->Init()) {
    return;
  }

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

  auto current_time = std::chrono::high_resolution_clock::now();
  auto last_time = current_time;
  auto delta_time = std::chrono::nanoseconds(0);

  auto scheduler_remaining = std::chrono::nanoseconds(0);
  auto frame_remaining = std::chrono::nanoseconds(0);

  while (true) {
    current_time = std::chrono::high_resolution_clock::now();
    delta_time = current_time - last_time;
    last_time = current_time;

    scheduler_remaining += delta_time;
    if (scheduler_remaining >= SCHEDULER_STEP) {
      scheduler_remaining -= SCHEDULER_STEP;

      auto start = current_time;
      dc_.scheduler()->Tick(SCHEDULER_STEP);
      auto end = std::chrono::high_resolution_clock::now();

      // save off delta for speed stats
      deltas_[delta_seq_++ % MAX_SCHEDULER_DELTAS] = end - start;
    }

    frame_remaining += delta_time;
    if (frame_remaining >= FRAME_STEP) {
      frame_remaining -= FRAME_STEP;

      PumpEvents();
      RenderFrame();
    }
  }
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

  int n = static_cast<int>(fread(dc_.bios(), sizeof(uint8_t), size, fp));
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

  int n = static_cast<int>(fread(dc_.flash(), sizeof(uint8_t), size, fp));
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

  uint8_t *data = reinterpret_cast<uint8_t *>(malloc(size));
  int n = static_cast<int>(fread(data, sizeof(uint8_t), size, fp));
  fclose(fp);

  if (n != size) {
    free(data);
    return false;
  }

  // load to 0x0c010000 (area 3) which is where 1ST_READ.BIN is normally
  // loaded to
  dc_.memory()->Memcpy(0x0c010000, data, size);
  free(data);

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

void Emulator::PumpEvents() {
  WindowEvent ev;

  wnd_.PumpEvents();

  while (wnd_.PollEvent(&ev)) {
    switch (ev.type) {
      case WE_KEY: {
        // let the profiler take a stab at the input first
        if (!Profiler::instance()->HandleInput(ev.key.code, ev.key.value)) {
          // debug tracing
          if (ev.key.code == K_F2) {
            if (ev.key.value) {
              ToggleTracing();
            }
          }
          // else, forward to maple
          else {
            dc_.maple()->HandleInput(0, ev.key.code, ev.key.value);
          }
        }
      } break;

      case WE_MOUSEMOVE: {
        Profiler::instance()->HandleMouseMove(ev.mousemove.x, ev.mousemove.y);
      } break;

      case WE_RESIZE: {
        rb_->ResizeVideo(ev.resize.width, ev.resize.height);
      } break;

      case WE_TTY: {
        LOG_INFO("tty input \"%s\"", ev.tty.buffer);
      } break;
    }
  }
}

void Emulator::ToggleTracing() {
  if (!trace_writer_) {
    char filename[PATH_MAX];
    GetNextTraceFilename(filename, sizeof(filename));

    trace_writer_ = new TraceWriter();

    if (!trace_writer_->Open(filename)) {
      delete trace_writer_;
      trace_writer_ = nullptr;

      LOG_INFO("Failed to start tracing");

      return;
    }

    LOG_INFO("Begin tracing to %s", filename);
  } else {
    delete trace_writer_;
    trace_writer_ = nullptr;

    LOG_INFO("End tracing");
  }

  dc_.set_trace_writer(trace_writer_);
}

void Emulator::RenderFrame() {
  rb_->BeginFrame();

  // render the last tile context
  TileContext *last_context = dc_.ta()->GetLastContext();

  if (last_context) {
    dc_.tile_renderer()->RenderContext(last_context, rb_);
  }

  // calculate scheduler speed
  auto total_delta = std::chrono::nanoseconds(0);
  for (unsigned i = 0; i < MAX_SCHEDULER_DELTAS; i++) {
    total_delta += deltas_[(delta_seq_ + i) % MAX_SCHEDULER_DELTAS];
  }
  float speed = ((SCHEDULER_STEP.count() * MAX_SCHEDULER_DELTAS) /
                 (float)total_delta.count()) *
                100.0f;

  // render stats
  char stats[512];
  snprintf(stats, sizeof(stats), "%.2f%%, %.2f rps", speed, dc_.pvr()->rps());
  rb_->RenderText2D(0, 0, 12.0f, 0xffffffff, stats);

  // render profiler
  Profiler::instance()->Render(rb_);

  rb_->EndFrame();
}
