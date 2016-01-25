#include <thread>
#include "core/core.h"
#include "emu/emulator.h"
#include "emu/profiler.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/texture_cache.h"
#include "hw/holly/tile_renderer.h"
#include "hw/maple/maple.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "renderer/gl_backend.h"
#include "trace/trace.h"

using namespace dvm;
using namespace dvm::emu;
using namespace dvm::hw;
using namespace dvm::hw::gdrom;
using namespace dvm::hw::holly;
using namespace dvm::renderer;
using namespace dvm::sys;
using namespace dvm::trace;

DEFINE_string(bios, "dc_bios.bin", "Path to BIOS");
DEFINE_string(flash, "dc_flash.bin", "Path to flash ROM");

Emulator::Emulator()
    : trace_writer_(nullptr),
      tile_renderer_(nullptr),
      core_events_(MAX_EVENTS),
      speed_() {
  rb_ = new GLBackend(window_);
}

Emulator::~Emulator() {
  delete rb_;
  delete trace_writer_;
  delete tile_renderer_;

  DestroyDreamcast(dc_);
}

void Emulator::Run(const char *path) {
  if (!window_.Init()) {
    return;
  }

  if (!rb_->Init()) {
    return;
  }

  if (!CreateDreamcast(dc_, rb_)) {
    return;
  }

  // setup tile renderer with the renderer backend and the dreamcast's
  // internal textue cache
  tile_renderer_ = new TileRenderer(*rb_, *dc_.texcache);

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
  running_ = true;

  // run core emulator in a separate thread
  std::thread cpu_thread(&Emulator::CoreThread, this);

  // run graphics in the current thread
  GraphicsThread();

  // wait until cpu thread finishes
  cpu_thread.join();
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

  int n = static_cast<int>(fread(dc_.bios, sizeof(uint8_t), size, fp));
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

  int n = static_cast<int>(fread(dc_.flash, sizeof(uint8_t), size, fp));
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
  dc_.memory->Memcpy(0x0c010000, data, size);
  free(data);

  dc_.sh4->SetPC(0x0c010000);

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

  dc_.trace_writer = trace_writer_;
}

void Emulator::GraphicsThread() {
  Profiler::ThreadScope thread_scope("graphics");

  while (running_.load(std::memory_order_relaxed)) {
    PumpGraphicsEvents();
    RenderGraphics();
  }
}

void Emulator::PumpGraphicsEvents() {
  WindowEvent ev;

  window_.PumpEvents();

  while (window_.PollEvent(&ev)) {
    switch (ev.type) {
      case WE_KEY: {
        // let the profiler take a stab at the input first
        if (!Profiler::instance().HandleInput(ev.key.code, ev.key.value)) {
          // else, forward to the CPU thread
          QueueCoreEvent(ev);
        }
      } break;

      case WE_MOUSEMOVE: {
        Profiler::instance().HandleMouseMove(ev.mousemove.x, ev.mousemove.y);
      } break;

      case WE_RESIZE: {
        rb_->ResizeVideo(ev.resize.width, ev.resize.height);
      } break;

      case WE_QUIT: {
        running_.store(false, std::memory_order_relaxed);
      } break;
    }
  }
}

void Emulator::RenderGraphics() {
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
  Profiler::instance().Render(rb_);

  rb_->EndFrame();
}

void Emulator::CoreThread() {
  Profiler::ThreadScope thread_scope("core");

  static const std::chrono::nanoseconds STEP = HZ_TO_NANO(1000);
  static const std::chrono::nanoseconds SAMPLE_PERIOD = HZ_TO_NANO(10);

  auto current_time = std::chrono::high_resolution_clock::now();
  auto last_time = current_time;

  auto next_step_time = current_time;
  auto next_sample_time = current_time;

  auto host_time = std::chrono::nanoseconds(0);
  auto guest_time = std::chrono::nanoseconds(0);

  while (running_.load(std::memory_order_relaxed)) {
    current_time = std::chrono::high_resolution_clock::now();
    last_time = current_time;

    // run scheduler every STEP nanoseconds
    if (current_time > next_step_time) {
      dc_.scheduler->Tick(STEP);

      host_time += std::chrono::high_resolution_clock::now() - last_time;
      guest_time += STEP;
      next_step_time = current_time + STEP;
    }

    // update speed every SAMPLE_PERIOD nanoseconds
    if (current_time > next_sample_time) {
      float speed = (guest_time.count() / static_cast<float>(host_time.count())) * 100.0f;
      speed_ = *reinterpret_cast<uint32_t *>(&speed);

      host_time = std::chrono::nanoseconds(0);
      guest_time = std::chrono::nanoseconds(0);
      next_sample_time = current_time + SAMPLE_PERIOD;
    }

    // handle events the graphics thread forwarded on
    PumpCoreEvents();
  }
}

void Emulator::QueueCoreEvent(const WindowEvent &ev) {
  std::lock_guard<std::mutex> guard(core_events_mutex_);

  if (core_events_.Full()) {
    LOG_WARNING("Core event overflow");
    return;
  }

  core_events_.PushBack(ev);
}

bool Emulator::PollCoreEvent(WindowEvent *ev) {
  std::lock_guard<std::mutex> guard(core_events_mutex_);

  if (core_events_.Empty()) {
    return false;
  }

  *ev = core_events_.front();
  core_events_.PopFront();

  return true;
}

void Emulator::PumpCoreEvents() {
  WindowEvent ev;

  while (PollCoreEvent(&ev)) {
    switch (ev.type) {
      case WE_KEY: {
        if (ev.key.code == K_F2) {
          if (ev.key.value) {
            ToggleTracing();
          }
        } else {
          dc_.maple->HandleInput(0, ev.key.code, ev.key.value);
        }
      } break;

      default: { CHECK(false, "Unexpected event type"); } break;
    }
  }
}
