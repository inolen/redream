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
using namespace dreavm::sys;
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
  texcache_ = new TextureCache(this);
  tile_renderer_ = new TileRenderer(*texcache_);
}

Dreamcast::~Dreamcast() {
  delete[] holly_regs_;
  delete[] pvr_regs_;

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
  delete texcache_;
  delete tile_renderer_;
}

bool Dreamcast::Init() {
  if (!MapMemory()) {
    return false;
  }

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

  if (!texcache_->Init()) {
    return false;
  }

  scheduler_->AddDevice(aica_);
  scheduler_->AddDevice(sh4_);

  return true;
}

bool Dreamcast::MapMemory() {
  if (!memory_->Init()) {
    return false;
  }

  // note, try to allocate as large of a chunk as possible (e.g. an entire area)
  // even if it's allocating some unneeded space. reason being, Windows has a
  // granularity of around 0x10000 for its file mapping APIs, which is much
  // smaller than some of the individual regions we're concerned with

  // area 0, 0x00000000 - 0x03ffffff
  memory_->Alloc(BIOS_START, (SH4_SQ_END & ADDR_MASK) - BIOS_START + 1,
                 MIRROR_MASK);

  bios_ = memory_->physical_base() + BIOS_START;
  flash_ = memory_->physical_base() + FLASH_START;
  palette_ram_ = memory_->physical_base() + PVR_PALETTE_START;
  aica_regs_ = memory_->physical_base() + AICA_REG_START;
  wave_ram_ = memory_->physical_base() + WAVE_RAM_START;

  memory_->Handle(HOLLY_REG_START, HOLLY_REG_SIZE, MIRROR_MASK, holly(),
                  &Holly::ReadRegister<uint8_t>,    //
                  &Holly::ReadRegister<uint16_t>,   //
                  &Holly::ReadRegister<uint32_t>,   //
                  nullptr,                          //
                  &Holly::WriteRegister<uint8_t>,   //
                  &Holly::WriteRegister<uint16_t>,  //
                  &Holly::WriteRegister<uint32_t>,  //
                  nullptr);

  memory_->Handle(PVR_REG_START, PVR_REG_SIZE, MIRROR_MASK, pvr(),
                  nullptr,               //
                  nullptr,               //
                  &PVR2::ReadRegister,   //
                  nullptr,               //
                  nullptr,               //
                  nullptr,               //
                  &PVR2::WriteRegister,  //
                  nullptr);

  memory_->Handle(AICA_REG_START, AICA_REG_SIZE, MIRROR_MASK, aica(),
                  nullptr,               //
                  nullptr,               //
                  &AICA::ReadRegister,   //
                  nullptr,               //
                  nullptr,               //
                  nullptr,               //
                  &AICA::WriteRegister,  //
                  nullptr);

  memory_->Handle(WAVE_RAM_START, WAVE_RAM_SIZE, MIRROR_MASK, aica(),
                  nullptr,           //
                  nullptr,           //
                  &AICA::ReadWave,   //
                  nullptr,           //
                  nullptr,           //
                  nullptr,           //
                  &AICA::WriteWave,  //
                  nullptr);

  // area 1, 0x04000000 - 0x07ffffff
  memory_->Alloc(PVR_VRAM32_START, PVR_VRAM64_END - PVR_VRAM32_START + 1,
                 MIRROR_MASK);

  video_ram_ = memory_->physical_base() + PVR_VRAM32_START;

  memory_->Handle(PVR_VRAM64_START, PVR_VRAM64_SIZE, MIRROR_MASK, pvr(),
                  &PVR2::ReadVRamInterleaved<uint8_t>,    //
                  &PVR2::ReadVRamInterleaved<uint16_t>,   //
                  &PVR2::ReadVRamInterleaved<uint32_t>,   //
                  nullptr,                                //
                  nullptr,                                //
                  &PVR2::WriteVRamInterleaved<uint16_t>,  //
                  &PVR2::WriteVRamInterleaved<uint32_t>,  //
                  nullptr);

  // area 2, 0x08000000 - 0x0Bffffff

  // area 3, 0x0c000000 - 0x0fffffff
  memory_->Alloc(MAIN_RAM_START, MAIN_RAM_END - MAIN_RAM_START + 1,
                 MAIN_RAM_MIRROR_MASK);
  ram_ = memory_->physical_base() + MAIN_RAM_START;

  // area 4, 0x10000000 - 0x13ffffff
  memory_->Alloc(TA_CMD_START, TA_TEXTURE_END - TA_CMD_START + 1, MIRROR_MASK);

  // TODO handle YUV transfers from 0x10800000 - 0x10ffffe0

  memory_->Handle(TA_CMD_START, TA_CMD_SIZE, 0x0, ta(),
                  nullptr,                         //
                  nullptr,                         //
                  nullptr,                         //
                  nullptr,                         //
                  nullptr,                         //
                  nullptr,                         //
                  &TileAccelerator::WriteCommand,  //
                  nullptr);

  memory_->Handle(TA_TEXTURE_START, TA_TEXTURE_SIZE, 0x0, ta(),
                  nullptr,                         //
                  nullptr,                         //
                  nullptr,                         //
                  nullptr,                         //
                  nullptr,                         //
                  nullptr,                         //
                  &TileAccelerator::WriteTexture,  //
                  nullptr);

  // area 5, 0x14000000 - 0x17ffffff
  memory_->Alloc(MODEM_START, MODEM_END - MODEM_START + 1, MIRROR_MASK);

  // area 6, 0x18000000 - 0x1bffffff
  memory_->Alloc(UNASSIGNED_START, UNASSIGNED_END - UNASSIGNED_START + 1,
                 MIRROR_MASK);

  // area 7, 0x1c000000 - 0x1fffffff
  memory_->Alloc(SH4_REG_START, SH4_REG_END - SH4_REG_START + 1, MIRROR_MASK);

  memory_->Handle(SH4_REG_START, SH4_REG_SIZE, MIRROR_MASK, sh4(),
                  &SH4::ReadRegister<uint8_t>,
                  &SH4::ReadRegister<uint16_t>,   //
                  &SH4::ReadRegister<uint32_t>,   //
                  nullptr,                        //
                  &SH4::WriteRegister<uint8_t>,   //
                  &SH4::WriteRegister<uint16_t>,  //
                  &SH4::WriteRegister<uint32_t>,  //
                  nullptr);

  memory_->Handle(SH4_CACHE_START, SH4_CACHE_SIZE, 0x0, sh4(),
                  &SH4::ReadCache<uint8_t>,    //
                  &SH4::ReadCache<uint16_t>,   //
                  &SH4::ReadCache<uint32_t>,   //
                  &SH4::ReadCache<uint64_t>,   //
                  &SH4::WriteCache<uint8_t>,   //
                  &SH4::WriteCache<uint16_t>,  //
                  &SH4::WriteCache<uint32_t>,  //
                  &SH4::WriteCache<uint64_t>);

  memory_->Handle(SH4_SQ_START, SH4_SQ_SIZE, 0x0, sh4(),
                  &SH4::ReadSQ<uint8_t>,    //
                  &SH4::ReadSQ<uint16_t>,   //
                  &SH4::ReadSQ<uint32_t>,   //
                  nullptr,                  //
                  &SH4::WriteSQ<uint8_t>,   //
                  &SH4::WriteSQ<uint16_t>,  //
                  &SH4::WriteSQ<uint32_t>,  //
                  nullptr);

  return true;
}
