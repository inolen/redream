#include "core/core.h"
#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/backend/x64/x64_backend.h"
#include "cpu/frontend/sh4/sh4_frontend.h"
#include "emu/dreamcast.h"
#include "emu/profiler.h"
#include "holly/maple_controller.h"
#include "renderer/gl_backend.h"

using namespace dreavm::aica;
using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::cpu::backend::interpreter;
using namespace dreavm::cpu::backend::x64;
using namespace dreavm::cpu::frontend::sh4;
using namespace dreavm::emu;
using namespace dreavm::gdrom;
using namespace dreavm::holly;
using namespace dreavm::renderer;
using namespace dreavm::system;
using namespace dreavm::trace;

DEFINE_string(bios, "dc_bios.bin", "Path to BIOS");
DEFINE_string(flash, "dc_flash.bin", "Path to flash ROM");

Dreamcast::Dreamcast() {
  scheduler_ = std::unique_ptr<Scheduler>(new Scheduler());
  memory_ = std::unique_ptr<Memory>(new Memory());
  rb_ = std::unique_ptr<renderer::Backend>(new GLBackend(sys_));
  rt_frontend_ =
      std::unique_ptr<frontend::Frontend>(new SH4Frontend(*memory()));
  rt_backend_ = std::unique_ptr<backend::Backend>(new X64Backend(*memory()));
  runtime_ = std::unique_ptr<Runtime>(
      new Runtime(*memory(), *rt_frontend_.get(), *rt_backend_.get()));
  cpu_ = std::unique_ptr<SH4>(new SH4(*memory(), *runtime()));
  aica_ = std::unique_ptr<AICA>(new AICA(this));
  holly_ = std::unique_ptr<Holly>(new Holly(this));
  pvr_ = std::unique_ptr<PVR2>(new PVR2(this));
  ta_ = std::unique_ptr<TileAccelerator>(new TileAccelerator(this));
  gdrom_ = std::unique_ptr<GDROM>(new GDROM(this));
  maple_ = std::unique_ptr<Maple>(new Maple(this));
}

void Dreamcast::Run(const char *path) {
  if (!Init()) {
    LOG_WARNING("Failed to initialize emulator");
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

  static const std::chrono::nanoseconds step = HZ_TO_NANO(60);
  std::chrono::nanoseconds time_remaining = std::chrono::nanoseconds(0);
  auto current_time = std::chrono::high_resolution_clock::now();
  auto last_time = current_time;

  while (true) {
    current_time = std::chrono::high_resolution_clock::now();
    time_remaining += current_time - last_time;
    last_time = current_time;

    if (time_remaining < step) {
      continue;
    }

    time_remaining -= step;

    PumpEvents();

    scheduler_->Tick(step);

    RenderFrame();
  }
}

bool Dreamcast::Init() {
  if (!sys_.Init()) {
    return false;
  }

  if (!rb_->Init()) {
    return false;
  }

  Profiler::Init();

  InitMemory();
  InitRegisters();

  cpu_->Init();
  aica_->Init();
  holly_->Init();
  pvr_->Init();
  ta_->Init();
  gdrom_->Init();
  maple_->Init();

  scheduler_->AddDevice(cpu());
  scheduler_->AddDevice(aica());

  return true;
}

void Dreamcast::InitMemory() {
  using namespace std::placeholders;

  memset(ram_, 0, sizeof(ram_));
  memset(unassigned_, 0, sizeof(unassigned_));
  memset(modem_mem_, 0, sizeof(modem_mem_));
  memset(aica_regs_, 0, sizeof(aica_regs_));
  memset(wave_ram_, 0, sizeof(wave_ram_));
  memset(expdev_mem_, 0, sizeof(expdev_mem_));
  memset(video_ram_, 0, sizeof(video_ram_));
  memset(palette_ram_, 0, sizeof(palette_ram_));

  // main ram
  memory_->Mount(BIOS_START, BIOS_END, MIRROR_MASK, bios_);
  memory_->Mount(FLASH_START, FLASH_END, MIRROR_MASK, flash_);
  memory_->Mount(MAIN_RAM_START, MAIN_RAM_END, MAIN_RAM_MIRROR_MASK, ram_);
  memory_->Mount(UNASSIGNED_START, UNASSIGNED_END, MIRROR_MASK, unassigned_);

  // aica
  memory_->Handle(AICA_REG_START, AICA_REG_END, MIRROR_MASK,
                  nullptr,                                            //
                  nullptr,                                            //
                  std::bind(&AICA::ReadRegister32, aica(), _1),       //
                  nullptr,                                            //
                  nullptr,                                            //
                  nullptr,                                            //
                  std::bind(&AICA::WriteRegister32, aica(), _1, _2),  //
                  nullptr);
  memory_->Handle(WAVE_RAM_START, WAVE_RAM_END, MIRROR_MASK,
                  nullptr,                                        //
                  nullptr,                                        //
                  std::bind(&AICA::ReadWave32, aica(), _1),       //
                  nullptr,                                        //
                  nullptr,                                        //
                  nullptr,                                        //
                  std::bind(&AICA::WriteWave32, aica(), _1, _2),  //
                  nullptr);

  // holly
  memory_->Handle(HOLLY_REG_START, HOLLY_REG_END, MIRROR_MASK,
                  nullptr,                                              //
                  nullptr,                                              //
                  std::bind(&Holly::ReadRegister32, holly(), _1),       //
                  nullptr,                                              //
                  nullptr,                                              //
                  nullptr,                                              //
                  std::bind(&Holly::WriteRegister32, holly(), _1, _2),  //
                  nullptr);
  memory_->Mount(MODEM_REG_START, MODEM_REG_END, MIRROR_MASK, modem_mem_);
  memory_->Mount(EXPDEV_START, EXPDEV_END, MIRROR_MASK, expdev_mem_);

  // gdrom
  memory_->Handle(GDROM_REG_START, GDROM_REG_END, MIRROR_MASK,
                  std::bind(&GDROM::ReadRegister8, gdrom(), _1),        //
                  std::bind(&GDROM::ReadRegister16, gdrom(), _1),       //
                  std::bind(&GDROM::ReadRegister32, gdrom(), _1),       //
                  nullptr,                                              //
                  std::bind(&GDROM::WriteRegister8, gdrom(), _1, _2),   //
                  std::bind(&GDROM::WriteRegister16, gdrom(), _1, _2),  //
                  std::bind(&GDROM::WriteRegister32, gdrom(), _1, _2),  //
                  nullptr);

  // maple
  memory_->Handle(MAPLE_REG_START, MAPLE_REG_END, MIRROR_MASK,
                  nullptr,                                              //
                  nullptr,                                              //
                  std::bind(&Maple::ReadRegister32, maple(), _1),       //
                  nullptr,                                              //
                  nullptr,                                              //
                  nullptr,                                              //
                  std::bind(&Maple::WriteRegister32, maple(), _1, _2),  //
                  nullptr);

  // pvr2
  memory_->Mount(PVR_VRAM32_START, PVR_VRAM32_END, MIRROR_MASK, video_ram_);
  memory_->Handle(PVR_VRAM64_START, PVR_VRAM64_END, MIRROR_MASK,
                  std::bind(&PVR2::ReadInterleaved8, pvr(), _1),        //
                  std::bind(&PVR2::ReadInterleaved16, pvr(), _1),       //
                  std::bind(&PVR2::ReadInterleaved32, pvr(), _1),       //
                  nullptr,                                              //
                  nullptr,                                              //
                  std::bind(&PVR2::WriteInterleaved16, pvr(), _1, _2),  //
                  std::bind(&PVR2::WriteInterleaved32, pvr(), _1, _2),  //
                  nullptr);
  memory_->Handle(PVR_REG_START, PVR_REG_END, MIRROR_MASK,
                  nullptr,                                           //
                  nullptr,                                           //
                  std::bind(&PVR2::ReadRegister32, pvr(), _1),       //
                  nullptr,                                           //
                  nullptr,                                           //
                  nullptr,                                           //
                  std::bind(&PVR2::WriteRegister32, pvr(), _1, _2),  //
                  nullptr);
  memory_->Mount(PVR_PALETTE_START, PVR_PALETTE_END, MIRROR_MASK, palette_ram_);

  // ta
  // TODO handle YUV transfers from 0x10800000 - 0x10ffffe0
  memory_->Handle(TA_CMD_START, TA_CMD_END, 0x0,
                  nullptr,                                                    //
                  nullptr,                                                    //
                  nullptr,                                                    //
                  nullptr,                                                    //
                  nullptr,                                                    //
                  nullptr,                                                    //
                  std::bind(&TileAccelerator::WriteCommand32, ta(), _1, _2),  //
                  nullptr);
  memory_->Handle(TA_TEXTURE_START, TA_TEXTURE_END, 0x0,
                  nullptr,                                                    //
                  nullptr,                                                    //
                  nullptr,                                                    //
                  nullptr,                                                    //
                  nullptr,                                                    //
                  nullptr,                                                    //
                  std::bind(&TileAccelerator::WriteTexture32, ta(), _1, _2),  //
                  nullptr);

  // cpu
  memory_->Handle(SH4_REG_START, SH4_REG_END, MIRROR_MASK,
                  std::bind(&SH4::ReadRegister8, cpu(), _1),        //
                  std::bind(&SH4::ReadRegister16, cpu(), _1),       //
                  std::bind(&SH4::ReadRegister32, cpu(), _1),       //
                  nullptr,                                          //
                  std::bind(&SH4::WriteRegister8, cpu(), _1, _2),   //
                  std::bind(&SH4::WriteRegister16, cpu(), _1, _2),  //
                  std::bind(&SH4::WriteRegister32, cpu(), _1, _2),  //
                  nullptr);
  memory_->Handle(SH4_CACHE_START, SH4_CACHE_END, 0x0,
                  std::bind(&SH4::ReadCache8, cpu(), _1),        //
                  std::bind(&SH4::ReadCache16, cpu(), _1),       //
                  std::bind(&SH4::ReadCache32, cpu(), _1),       //
                  std::bind(&SH4::ReadCache64, cpu(), _1),       //
                  std::bind(&SH4::WriteCache8, cpu(), _1, _2),   //
                  std::bind(&SH4::WriteCache16, cpu(), _1, _2),  //
                  std::bind(&SH4::WriteCache32, cpu(), _1, _2),  //
                  std::bind(&SH4::WriteCache64, cpu(), _1, _2));
  memory_->Handle(SH4_SQ_START, SH4_SQ_END, 0x0,
                  std::bind(&SH4::ReadSQ8, cpu(), _1),        //
                  std::bind(&SH4::ReadSQ16, cpu(), _1),       //
                  std::bind(&SH4::ReadSQ32, cpu(), _1),       //
                  nullptr,                                    //
                  std::bind(&SH4::WriteSQ8, cpu(), _1, _2),   //
                  std::bind(&SH4::WriteSQ16, cpu(), _1, _2),  //
                  std::bind(&SH4::WriteSQ32, cpu(), _1, _2),  //
                  nullptr);
}

void Dreamcast::InitRegisters() {
#define HOLLY_REG(addr, name, flags, default, type) \
  holly_regs_[name##_OFFSET] = {flags, default};
#include "holly/holly_regs.inc"
#undef HOLLY_REG

#define PVR_REG(addr, name, flags, default, type) \
  pvr_regs_[name##_OFFSET] = {flags, default};
#include "holly/pvr2_regs.inc"
#undef PVR_REG
}

bool Dreamcast::LoadBios(const char *path) {
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

bool Dreamcast::LoadFlash(const char *path) {
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

bool Dreamcast::LaunchBIN(const char *path) {
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

  cpu_->SetPC(0x0c010000);

  return true;
}

bool Dreamcast::LaunchGDI(const char *path) {
  std::unique_ptr<GDI> gdi(new GDI());

  if (!gdi->Load(path)) {
    return false;
  }

  gdrom_->SetDisc(std::move(gdi));
  cpu_->SetPC(0xa0000000);

  return true;
}

void Dreamcast::PumpEvents() {
  SystemEvent ev;

  sys_.PumpEvents();

  while (sys_.PollEvent(&ev)) {
    switch (ev.type) {
      case SE_KEY: {
        // let the profiler take a stab at the input first
        if (!Profiler::HandleInput(ev.key.code, ev.key.value)) {
          // debug tracing
          if (ev.key.code == K_F2) {
            if (ev.key.value) {
              ToggleTracing();
            }
          }
          // else, forward to maple
          else {
            maple_->HandleInput(0, ev.key.code, ev.key.value);
          }
        }
      } break;

      case SE_MOUSEMOVE: {
        Profiler::HandleMouseMove(ev.mousemove.x, ev.mousemove.y);
      } break;

      case SE_RESIZE: {
        rb_->ResizeVideo(ev.resize.width, ev.resize.height);
      } break;
    }
  }
}

void Dreamcast::ToggleTracing() {
  if (!trace_writer_) {
    char filename[PATH_MAX];
    GetNextTraceFilename(filename, sizeof(filename));

    trace_writer_ = std::unique_ptr<TraceWriter>(new TraceWriter());

    if (!trace_writer_->Open(filename)) {
      trace_writer_ = nullptr;

      LOG_INFO("Failed to start tracing");

      return;
    }

    LOG_INFO("Begin tracing to %s", filename);
  } else {
    trace_writer_ = nullptr;

    LOG_INFO("End tracing");
  }
}

void Dreamcast::RenderFrame() {
  rb_->BeginFrame();

  ta_->RenderLastContext();

  // render stats
  char stats[512];
  snprintf(stats, sizeof(stats), "%.2f fps, %.2f vbps", pvr_->fps(),
           pvr_->vbps());
  rb_->RenderText2D(0, 0, 12.0f, 0xffffffff, stats);

  // render profiler
  Profiler::Render(rb());

  rb_->EndFrame();
}
