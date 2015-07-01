#include "core/core.h"
#include "emu/emulator.h"
#include "emu/profiler.h"
#include "holly/maple_controller.h"
#include "renderer/gl_backend.h"

using namespace dreavm;
using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::interpreter;
using namespace dreavm::cpu::frontend::sh4;
using namespace dreavm::emu;
using namespace dreavm::holly;
using namespace dreavm::renderer;
using namespace dreavm::system;

Emulator::Emulator(System &sys)
    : sys_(sys),
      scheduler_(new Scheduler()),
      memory_(new Memory()),
      sh4_frontend_(new SH4Frontend(*memory_)),
      int_backend_(new InterpreterBackend(*memory_)),
      runtime_(new Runtime(*memory_)),
      processor_(new SH4(*scheduler_, *memory_)),
      holly_(new Holly(*scheduler_, *memory_, *processor_)) {
  rb_ = new GLBackend(sys);
}

Emulator::~Emulator() {
  Profiler::Shutdown();
  delete rb_;
  delete holly_;
  delete processor_;
  delete runtime_;
  delete int_backend_;
  delete sh4_frontend_;
  delete memory_;
  delete scheduler_;
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

  if (!LoadBios("../dreamcast/dc_bios.bin")) {
    return false;
  }

  if (!LoadFlash("../dreamcast/dc_flash.bin")) {
    return false;
  }

  if (!runtime_->Init(sh4_frontend_, int_backend_)) {
    return false;
  }

  // order here is important, sh4 memory handlers need to override
  // some of the broader holly handlers
  if (!holly_->Init(rb_)) {
    return false;
  }

  if (!processor_->Init(runtime_)) {
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

  uint8_t *data = (uint8_t *)malloc(size);
  int n = fread(data, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    free(data);
    return false;
  }

  // load to 0x0c010000 (area 3) which is where 1ST_READ.BIN is normally
  // loaded to
  memory_->Memcpy(0x0c010000, data, size);
  free(data);
  return true;
}

bool Emulator::LaunchGDI(const char *path) {
  std::unique_ptr<GDI> gdi(new GDI());

  if (!gdi->Load(path)) {
    return false;
  }

  holly_->gdrom().SetDisc(std::move(gdi));

  return true;
}

void Emulator::Tick() {
  PumpEvents();

  scheduler_->Tick();

  RenderFrame();
}

// for memory mapping notes, see 2.1 System Mapping in the hardware manual
bool Emulator::MountRam() {
  memory_->Alloc(0x0, 0x1fffffff, 0xe0000000);
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

  memory_->Memcpy(BIOS_START, data, size);
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

  memory_->Memcpy(FLASH_START, data, size);
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
        holly_->HandleInput(0, ev.key.code, ev.key.value);
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
           scheduler_->perf(), holly_->fps(), holly_->vbps());
  LOG_EVERY_N(INFO, 10) << stats;
  rb_->RenderText2D(0.0f, 0.0f, 12, 0xffffffff, stats);

  // render profiler
  Profiler::Render(rb_);

  rb_->EndFrame();
}
