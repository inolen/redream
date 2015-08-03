#include "core/core.h"
#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/backend/x64/x64_backend.h"
#include "cpu/frontend/sh4/sh4_frontend.h"
#include "emu/emulator.h"
#include "emu/profiler.h"
#include "holly/maple_controller.h"
#include "renderer/gl_backend.h"

using namespace dreavm;
using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::interpreter;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::frontend::sh4;
using namespace dreavm::emu;
using namespace dreavm::holly;
using namespace dreavm::renderer;
using namespace dreavm::system;

DEFINE_string(bios, "dc_bios.bin", "Path to BIOS");
DEFINE_string(flash, "dc_flash.bin", "Path to flash ROM");

Emulator::Emulator(System &sys)
    : sys_(sys),
      runtime_(memory_),
      processor_(scheduler_, memory_),
      holly_(scheduler_, memory_, processor_) {
  rt_frontend_ = new SH4Frontend(memory_);
  // rt_backend_ = new InterpreterBackend(memory_);
  rt_backend_ = new X64Backend(memory_);
  rb_ = new GLBackend(sys);
}

Emulator::~Emulator() {
  Profiler::Shutdown();
  delete rb_;
  delete rt_backend_;
  delete rt_frontend_;
}

bool Emulator::Init() {
  if (!rb_->Init()) {
    return false;
  }

  if (!Profiler::Init()) {
    return false;
  }

  if (!MountRam()) {
    return false;
  }

  if (!LoadBios(FLAGS_bios.c_str())) {
    return false;
  }

  if (!LoadFlash(FLAGS_flash.c_str())) {
    return false;
  }

  if (!runtime_.Init(rt_frontend_, rt_backend_)) {
    return false;
  }

  if (!holly_.Init(rb_)) {
    return false;
  }

  if (!processor_.Init(&runtime_)) {
    return false;
  }

  return true;
}

bool Emulator::Launch(const char *path) {
  LOG(INFO) << "Launching " << path;

  if (strstr(path, ".bin")) {
    return LaunchBIN(path);
  } else if (strstr(path, ".gdi")) {
    return LaunchGDI(path);
  }

  return false;
}

bool Emulator::LaunchBIN(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  uint8_t *data = (uint8_t *)malloc(size);
  int n = fread(data, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    free(data);
    return false;
  }

  // load to 0x0c010000 (area 3) which is where 1ST_READ.BIN is normally
  // loaded to
  memory_.Memcpy(0x0c010000, data, size);
  free(data);

  // restart to where the bin was loaded
  processor_.Reset(0x0c010000);

  return true;
}

bool Emulator::LaunchGDI(const char *path) {
  std::unique_ptr<GDI> gdi(new GDI());

  if (!gdi->Load(path)) {
    return false;
  }

  holly_.gdrom().SetDisc(std::move(gdi));

  // restart to bios
  processor_.Reset(0xa0000000);

  return true;
}

void Emulator::Tick() {
  PumpEvents();

  scheduler_.Tick();

  RenderFrame();
}

// for memory mapping notes, see 2.1 System Mapping in the hardware manual
bool Emulator::MountRam() {
  memory_.Alloc(0x0, 0x1fffffff, 0xe0000000);
  return true;
}

bool Emulator::LoadBios(const char *path) {
  static const int BIOS_START = 0x00000000;
  static const int BIOS_SIZE = 0x200000;

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    LOG(WARNING) << "Failed to open bios at \"" << path << "\"";
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != BIOS_SIZE) {
    LOG(WARNING) << "Bios size mismatch, is " << size << ", expected "
                 << BIOS_SIZE;
    fclose(fp);
    return false;
  }

  uint8_t *data = (uint8_t *)malloc(size);
  int n = fread(data, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    LOG(WARNING) << "Bios read failed";
    free(data);
    return false;
  }

  memory_.Memcpy(BIOS_START, data, size);
  free(data);
  return true;
}

bool Emulator::LoadFlash(const char *path) {
  static const int FLASH_START = 0x00200000;
  static const int FLASH_SIZE = 0x20000;

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    LOG(WARNING) << "Failed to open flash at \"" << path << "\"";
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != FLASH_SIZE) {
    LOG(WARNING) << "Flash size mismatch, is " << size << ", expected "
                 << FLASH_SIZE;
    fclose(fp);
    return false;
  }

  uint8_t *data = (uint8_t *)malloc(size);
  int n = fread(data, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    LOG(WARNING) << "Flash read failed";
    free(data);
    return false;
  }

  memory_.Memcpy(FLASH_START, data, size);
  free(data);
  return true;
}

void Emulator::PumpEvents() {
  SystemEvent ev;
  while (sys_.PollEvent(&ev)) {
    if (ev.type == SE_KEY) {
      // let the profiler take a stab at the input first
      if (!Profiler::HandleInput(ev.key.code, ev.key.value)) {
        // else, forward to holly
        holly_.maple().HandleInput(0, ev.key.code, ev.key.value);
      }
    } else if (ev.type == SE_MOUSEMOVE) {
      Profiler::HandleMouseMove(ev.mousemove.x, ev.mousemove.y);
    } else if (ev.type == SE_RESIZE) {
      rb_->ResizeFramebuffer(FB_DEFAULT, ev.resize.width, ev.resize.height);
    }
  }
}

void Emulator::RenderFrame() {
  rb_->BeginFrame();

  // render latest TA output
  rb_->RenderTA();

  // render stats
  char stats[512];
  snprintf(stats, sizeof(stats), "%.2f%%, %.2f fps, %.2f vbps",
           scheduler_.perf(), holly_.pvr().fps(), holly_.pvr().vbps());
  LOG_EVERY_N(INFO, 10) << stats;
  rb_->RenderText2D(0.0f, 0.0f, 12, 0xffffffff, stats);

  // render profiler
  Profiler::Render(rb_);

  rb_->EndFrame();
}
