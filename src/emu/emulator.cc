#include "core/core.h"
#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/backend/x64/x64_backend.h"
#include "cpu/frontend/sh4/sh4_frontend.h"
#include "emu/emulator.h"
#include "emu/profiler.h"
#include "holly/maple_controller.h"
#include "renderer/gl_backend.h"

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

Emulator::Emulator(System &sys) : sys_(sys) {
  bios_ = new uint8_t[BIOS_SIZE];
  flash_ = new uint8_t[FLASH_SIZE];
  ram_ = new uint8_t[MAIN_RAM_M0_SIZE];
  unassigned_ = new uint8_t[UNASSIGNED_SIZE];

  scheduler_ = new Scheduler();
  memory_ = new Memory();
  runtime_ = new Runtime(*memory_);
  processor_ = new SH4(*scheduler_, *memory_);
  holly_ = new Holly(*scheduler_, *memory_, *processor_);
  rt_frontend_ = new SH4Frontend(*memory_);
  rt_backend_ = new InterpreterBackend(*memory_);
  // rt_backend_ = new X64Backend(*memory_);
  rb_ = new GLBackend(sys);
}

Emulator::~Emulator() {
  Profiler::Shutdown();

  delete[] bios_;
  delete[] flash_;
  delete[] ram_;
  delete[] unassigned_;

  delete scheduler_;
  delete memory_;
  delete runtime_;
  delete processor_;
  delete holly_;
  delete rt_frontend_;
  delete rt_backend_;
  delete rb_;
}

bool Emulator::Init() {
  InitMemory();

  if (!rb_->Init()) {
    return false;
  }

  if (!Profiler::Init()) {
    return false;
  }

  if (!LoadBios(FLAGS_bios.c_str())) {
    return false;
  }

  if (!LoadFlash(FLAGS_flash.c_str())) {
    return false;
  }

  if (!runtime_->Init(rt_frontend_, rt_backend_)) {
    return false;
  }

  if (!holly_->Init(rb_)) {
    return false;
  }

  if (!processor_->Init(runtime_)) {
    return false;
  }

  Reset();

  return true;
}

bool Emulator::Launch(const char *path) {
  LOG_INFO("Launching %s", path);

  if (strstr(path, ".bin")) {
    return LaunchBIN(path);
  } else if (strstr(path, ".gdi")) {
    return LaunchGDI(path);
  }

  return false;
}

void Emulator::Tick() {
  PumpEvents();

  scheduler_->Tick();

  RenderFrame();
}

void Emulator::InitMemory() {
  memory_->Mount(BIOS_START, BIOS_END, MIRROR_MASK, bios_);
  memory_->Mount(FLASH_START, FLASH_END, MIRROR_MASK, flash_);
  memory_->Mount(MAIN_RAM_M0_START, MAIN_RAM_M0_END, MIRROR_MASK, ram_);
  memory_->Mount(MAIN_RAM_M1_START, MAIN_RAM_M1_END, MIRROR_MASK, ram_);
  memory_->Mount(MAIN_RAM_M2_START, MAIN_RAM_M2_END, MIRROR_MASK, ram_);
  memory_->Mount(MAIN_RAM_M3_START, MAIN_RAM_M3_END, MIRROR_MASK, ram_);
  memory_->Mount(UNASSIGNED_START, UNASSIGNED_END, MIRROR_MASK, unassigned_);
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

  int n = static_cast<int>(fread(bios_, sizeof(uint8_t), size, fp));
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

  int n = static_cast<int>(fread(flash_, sizeof(uint8_t), size, fp));
  fclose(fp);

  if (n != size) {
    LOG_WARNING("Flash read failed");
    return false;
  }

  return true;
}

void Emulator::Reset() {
  memset(ram_, 0, MAIN_RAM_M0_SIZE);
  memset(unassigned_, 0, UNASSIGNED_SIZE);
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
  memory_->Memcpy(0x0c010000, data, size);
  free(data);

  // restart to where the bin was loaded
  processor_->Reset(0x0c010000);

  return true;
}

bool Emulator::LaunchGDI(const char *path) {
  std::unique_ptr<GDI> gdi(new GDI());

  if (!gdi->Load(path)) {
    return false;
  }

  holly_->gdrom().SetDisc(std::move(gdi));

  // restart to bios
  processor_->Reset(0xa0000000);

  return true;
}

void Emulator::PumpEvents() {
  SystemEvent ev;

  while (sys_.PollEvent(&ev)) {
    if (ev.type == SE_KEY) {
      // let the profiler take a stab at the input first
      if (!Profiler::HandleInput(ev.key.code, ev.key.value)) {
        // debug tracing
        if (ev.key.code == K_F2) {
          if (ev.key.value) {
            holly_->pvr().ToggleTracing();
          }
        }
        // else, forward to maple
        else {
          holly_->maple().HandleInput(0, ev.key.code, ev.key.value);
        }
      }
    } else if (ev.type == SE_MOUSEMOVE) {
      Profiler::HandleMouseMove(ev.mousemove.x, ev.mousemove.y);
    } else if (ev.type == SE_RESIZE) {
      rb_->SetFramebufferSize(FB_DEFAULT, ev.resize.width, ev.resize.height);
    }
  }
}

void Emulator::RenderFrame() {
  rb_->BeginFrame();

  // render latest TA output
  rb_->RenderFramebuffer(FB_TILE_ACCELERATOR);

  // render stats
  char stats[512];
  snprintf(stats, sizeof(stats), "%.2f%%, %.2f fps, %.2f vbps",
           scheduler_->perf(), holly_->pvr().fps(), holly_->pvr().vbps());
  // LOG_EVERY_N(INFO, 10) << stats;
  rb_->RenderText2D(0, 0, 12.0f, 0xffffffff, stats);

  // render profiler
  Profiler::Render(rb_);

  rb_->EndFrame();
}
