#include "core/core.h"
#include "hw/dreamcast.h"
#include "jit/backend/interpreter/interpreter_backend.h"
#include "jit/backend/x64/x64_backend.h"
#include "jit/frontend/sh4/sh4_frontend.h"

using namespace dreavm;
using namespace dreavm::hw;
using namespace dreavm::hw::aica;
using namespace dreavm::hw::gdrom;
using namespace dreavm::hw::holly;
using namespace dreavm::hw::maple;
using namespace dreavm::hw::sh4;
using namespace dreavm::jit;
using namespace dreavm::jit::backend::interpreter;
using namespace dreavm::jit::backend::x64;
using namespace dreavm::jit::frontend::sh4;
using namespace dreavm::renderer;
using namespace dreavm::trace;

Dreamcast::Dreamcast()
    :  // allocate registers and initialize references
      holly_regs_(new Register[HOLLY_REG_SIZE >> 2]),
#define HOLLY_REG(offset, name, flags, default, type) \
  name{reinterpret_cast<type &>(holly_regs_[name##_OFFSET].value)},
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG
      pvr_regs_(new Register[PVR_REG_SIZE >> 2]),
#define PVR_REG(offset, name, flags, default, type) \
  name{reinterpret_cast<type &>(pvr_regs_[name##_OFFSET].value)},
#include "hw/holly/pvr2_regs.inc"
#undef PVR_REG
      rb_(nullptr),
      trace_writer_(nullptr) {
// initialize register values
#define HOLLY_REG(addr, name, flags, default, type) \
  holly_regs_[name##_OFFSET] = {flags, default};
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG

#define PVR_REG(addr, name, flags, default, type) \
  pvr_regs_[name##_OFFSET] = {flags, default};
#include "hw/holly/pvr2_regs.inc"
#undef PVR_REG

  bios_ = new uint8_t[BIOS_SIZE]();
  flash_ = new uint8_t[FLASH_SIZE]();
  ram_ = new uint8_t[MAIN_RAM_SIZE]();
  unassigned_ = new uint8_t[UNASSIGNED_SIZE]();
  modem_mem_ = new uint8_t[MODEM_REG_SIZE]();
  aica_regs_ = new uint8_t[AICA_REG_SIZE]();
  wave_ram_ = new uint8_t[WAVE_RAM_SIZE]();
  expdev_mem_ = new uint8_t[EXPDEV_SIZE]();
  video_ram_ = new uint8_t[PVR_VRAM32_SIZE]();
  palette_ram_ = new uint8_t[PVR_PALETTE_SIZE]();

  scheduler_ = new Scheduler();
  memory_ = new Memory();
  rt_frontend_ = new SH4Frontend(*memory_);
  rt_backend_ = new X64Backend(*memory_);
  runtime_ = new Runtime(*memory_, *rt_frontend_, *rt_backend_);
  aica_ = new AICA(this);
  gdrom_ = new GDROM(this);
  holly_ = new Holly(this);
  maple_ = new Maple(this);
  pvr_ = new PVR2(this);
  sh4_ = new SH4(*memory_, *runtime_);
  ta_ = new TileAccelerator(this);
}

Dreamcast::~Dreamcast() {
  delete[] holly_regs_;
  delete[] pvr_regs_;

  delete bios_;
  delete flash_;
  delete ram_;
  delete unassigned_;
  delete modem_mem_;
  delete aica_regs_;
  delete wave_ram_;
  delete expdev_mem_;
  delete video_ram_;
  delete palette_ram_;

  delete scheduler_;
  delete memory_;
  delete rt_frontend_;
  delete rt_backend_;
  delete runtime_;
  delete aica_;
  delete gdrom_;
  delete holly_;
  delete maple_;
  delete pvr_;
  delete sh4_;
  delete ta_;
}

bool Dreamcast::Init() {
  MapMemory();

  if (!aica_->Init()) {
    return false;
  }

  if (!gdrom_->Init()) {
    return false;
  }

  if (!holly_->Init()) {
    return false;
  }

  if (!maple_->Init()) {
    return false;
  }

  if (!pvr_->Init()) {
    return false;
  }

  if (!sh4_->Init()) {
    return false;
  }

  if (!ta_->Init()) {
    return false;
  }

  scheduler_->AddDevice(aica_);
  scheduler_->AddDevice(sh4_);

  return true;
}

void Dreamcast::MapMemory() {
  using namespace std::placeholders;

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
                  std::bind(&SH4::ReadRegister8, sh4(), _1),        //
                  std::bind(&SH4::ReadRegister16, sh4(), _1),       //
                  std::bind(&SH4::ReadRegister32, sh4(), _1),       //
                  nullptr,                                          //
                  std::bind(&SH4::WriteRegister8, sh4(), _1, _2),   //
                  std::bind(&SH4::WriteRegister16, sh4(), _1, _2),  //
                  std::bind(&SH4::WriteRegister32, sh4(), _1, _2),  //
                  nullptr);
  memory_->Handle(SH4_CACHE_START, SH4_CACHE_END, 0x0,
                  std::bind(&SH4::ReadCache8, sh4(), _1),        //
                  std::bind(&SH4::ReadCache16, sh4(), _1),       //
                  std::bind(&SH4::ReadCache32, sh4(), _1),       //
                  std::bind(&SH4::ReadCache64, sh4(), _1),       //
                  std::bind(&SH4::WriteCache8, sh4(), _1, _2),   //
                  std::bind(&SH4::WriteCache16, sh4(), _1, _2),  //
                  std::bind(&SH4::WriteCache32, sh4(), _1, _2),  //
                  std::bind(&SH4::WriteCache64, sh4(), _1, _2));
  memory_->Handle(SH4_SQ_START, SH4_SQ_END, 0x0,
                  std::bind(&SH4::ReadSQ8, sh4(), _1),        //
                  std::bind(&SH4::ReadSQ16, sh4(), _1),       //
                  std::bind(&SH4::ReadSQ32, sh4(), _1),       //
                  nullptr,                                    //
                  std::bind(&SH4::WriteSQ8, sh4(), _1, _2),   //
                  std::bind(&SH4::WriteSQ16, sh4(), _1, _2),  //
                  std::bind(&SH4::WriteSQ32, sh4(), _1, _2),  //
                  nullptr);
}
