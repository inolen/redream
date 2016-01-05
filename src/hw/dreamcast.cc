#include "core/core.h"
#include "hw/aica/aica.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/texture_cache.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/holly/tile_renderer.h"
#include "hw/maple/maple.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "renderer/backend.h"
#include "trace/trace.h"

using namespace dreavm;
using namespace dreavm::hw;
using namespace dreavm::hw::aica;
using namespace dreavm::hw::gdrom;
using namespace dreavm::hw::holly;
using namespace dreavm::hw::maple;
using namespace dreavm::hw::sh4;
using namespace dreavm::jit;
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
  aica_ = new AICA(this);
  gdrom_ = new GDROM(this);
  holly_ = new Holly(this);
  maple_ = new Maple(this);
  pvr_ = new PVR2(this);
  sh4_ = new SH4(*memory_);
  ta_ = new TileAccelerator(this);
  texcache_ = new TextureCache(this);
  tile_renderer_ = new TileRenderer(*texcache_);
}

Dreamcast::~Dreamcast() {
  delete[] holly_regs_;
  delete[] pvr_regs_;

  delete scheduler_;
  delete memory_;
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
  scheduler_->AddDevice(pvr_);
  scheduler_->AddDevice(sh4_);

  return true;
}

// clang-format off
bool Dreamcast::MapMemory() {
  if (!memory_->Init()) {
    return false;
  }

  // first, allocate static regions
  RegionHandle a0_handle = memory_->AllocRegion(AREA0_START, AREA0_SIZE);
  RegionHandle a1_handle = memory_->AllocRegion(AREA1_START, AREA1_SIZE);
  // area 2 unused
  RegionHandle a3_handle = memory_->AllocRegion(AREA3_START, AREA3_SIZE);
  // area 4 unused
  RegionHandle a5_handle = memory_->AllocRegion(AREA5_START, AREA5_SIZE);
  RegionHandle a6_handle = memory_->AllocRegion(AREA6_START, AREA6_SIZE);
  RegionHandle a7_handle = memory_->AllocRegion(AREA7_START, AREA7_SIZE);

  // second, allocate dynamic regions that overlap static regions
  RegionHandle holly_handle = memory_->AllocRegion(
    HOLLY_REG_START, HOLLY_REG_SIZE, holly(),
    &Holly::ReadRegister<uint8_t>,
    &Holly::ReadRegister<uint16_t>,
    &Holly::ReadRegister<uint32_t>,
    nullptr,
    &Holly::WriteRegister<uint8_t>,
    &Holly::WriteRegister<uint16_t>,
    &Holly::WriteRegister<uint32_t>,
    nullptr);
  RegionHandle pvr_reg_handle = memory_->AllocRegion(
    PVR_REG_START, PVR_REG_SIZE, pvr(),
    nullptr,
    nullptr,
    &PVR2::ReadRegister,
    nullptr,
    nullptr,
    nullptr,
    &PVR2::WriteRegister,
    nullptr);
  RegionHandle aica_reg_handle = memory_->AllocRegion(
    AICA_REG_START, AICA_REG_SIZE, aica(),
    nullptr,
    nullptr,
    &AICA::ReadRegister,
    nullptr,
    nullptr,
    nullptr,
    &AICA::WriteRegister,
    nullptr);
  RegionHandle wave_ram_handle = memory_->AllocRegion(
    WAVE_RAM_START, WAVE_RAM_SIZE, aica(),
    nullptr,
    nullptr,
    &AICA::ReadWave,
    nullptr,
    nullptr,
    nullptr,
    &AICA::WriteWave,
    nullptr);
  RegionHandle pvr_vram64_handle = memory_->AllocRegion(
    PVR_VRAM64_START, PVR_VRAM64_SIZE, pvr(),
    &PVR2::ReadVRamInterleaved<uint8_t>,
    &PVR2::ReadVRamInterleaved<uint16_t>,
    &PVR2::ReadVRamInterleaved<uint32_t>,
    nullptr,
    nullptr,
    &PVR2::WriteVRamInterleaved<uint16_t>,
    &PVR2::WriteVRamInterleaved<uint32_t>,
    nullptr);
  RegionHandle ta_cmd_handle = memory_->AllocRegion(
    TA_CMD_START, TA_CMD_SIZE, ta(),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &TileAccelerator::WriteCommand,
    nullptr);
  RegionHandle ta_texture_handle = memory_->AllocRegion(
    TA_TEXTURE_START, TA_TEXTURE_SIZE, ta(),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &TileAccelerator::WriteTexture,
    nullptr);
  RegionHandle sh4_reg_handle = memory_->AllocRegion(
    SH4_REG_START, SH4_REG_SIZE, sh4(),
    &SH4::ReadRegister<uint8_t>,
    &SH4::ReadRegister<uint16_t>,
    &SH4::ReadRegister<uint32_t>,
    nullptr,
    &SH4::WriteRegister<uint8_t>,
    &SH4::WriteRegister<uint16_t>,
    &SH4::WriteRegister<uint32_t>,
    nullptr);
  RegionHandle sh4_cache_handle = memory_->AllocRegion(
    SH4_CACHE_START, SH4_CACHE_SIZE, sh4(),
    &SH4::ReadCache<uint8_t>,
    &SH4::ReadCache<uint16_t>,
    &SH4::ReadCache<uint32_t>,
    &SH4::ReadCache<uint64_t>,
    &SH4::WriteCache<uint8_t>,
    &SH4::WriteCache<uint16_t>,
    &SH4::WriteCache<uint32_t>,
    &SH4::WriteCache<uint64_t>);
   RegionHandle sh4_sq_handle = memory_->AllocRegion(
    SH4_SQ_START, SH4_SQ_SIZE, sh4(),
    &SH4::ReadSQ<uint8_t>,
    &SH4::ReadSQ<uint16_t>,
    &SH4::ReadSQ<uint32_t>,
    nullptr,
    &SH4::WriteSQ<uint8_t>,
    &SH4::WriteSQ<uint16_t>,
    &SH4::WriteSQ<uint32_t>,
    nullptr);

  // setup memory mapping for the allocated regions
  MemoryMap memmap;

  // mount physical regions to their respective physical address
  memmap.Mount(a0_handle, AREA0_SIZE, AREA0_START);
  memmap.Mount(a1_handle, AREA1_SIZE, AREA1_START);
  memmap.Mount(a3_handle, AREA3_SIZE, AREA3_START);
  memmap.Mount(a5_handle, AREA5_SIZE, AREA5_START);
  memmap.Mount(a6_handle, AREA6_SIZE, AREA6_START);
  memmap.Mount(a7_handle, AREA7_SIZE, AREA7_START);
  memmap.Mount(holly_handle, HOLLY_REG_SIZE, HOLLY_REG_START);
  memmap.Mount(pvr_reg_handle, PVR_REG_SIZE, PVR_REG_START);
  memmap.Mount(aica_reg_handle, AICA_REG_SIZE, AICA_REG_START);
  memmap.Mount(wave_ram_handle, WAVE_RAM_SIZE, WAVE_RAM_START);
  memmap.Mount(pvr_vram64_handle, PVR_VRAM64_SIZE, PVR_VRAM64_START);
  memmap.Mount(ta_cmd_handle, TA_CMD_SIZE, TA_CMD_START);
  memmap.Mount(ta_texture_handle, TA_TEXTURE_SIZE, TA_TEXTURE_START);
  memmap.Mount(sh4_reg_handle, SH4_REG_SIZE, SH4_REG_START);

  // main ram mirrors
  memmap.Mirror(MAIN_RAM_1_START, MAIN_RAM_1_SIZE, MAIN_RAM_2_START);
  memmap.Mirror(MAIN_RAM_1_START, MAIN_RAM_1_SIZE, MAIN_RAM_3_START);
  memmap.Mirror(MAIN_RAM_1_START, MAIN_RAM_1_SIZE, MAIN_RAM_4_START);

  // physical mirrors (ignoring p, alt and cache bits in bits 31-29)
  memmap.Mirror(P0_1_START, P0_1_SIZE, P0_2_START);
  memmap.Mirror(P0_1_START, P0_1_SIZE, P0_3_START);
  memmap.Mirror(P0_1_START, P0_1_SIZE, P0_4_START);
  memmap.Mirror(P0_1_START, P0_1_SIZE, P1_START);
  memmap.Mirror(P0_1_START, P0_1_SIZE, P2_START);
  memmap.Mirror(P0_1_START, P0_1_SIZE, P3_START);
  memmap.Mirror(P0_1_START, P0_1_SIZE, P4_START);

  // handle some special access only available in P4 after applying mirrors
  memmap.Mount(sh4_cache_handle, SH4_CACHE_SIZE, SH4_CACHE_START);
  memmap.Mount(sh4_sq_handle, SH4_SQ_SIZE, SH4_SQ_START);

  if (!memory_->Map(memmap)) {
    return false;
  }

  bios_ = memory_->virtual_base() + BIOS_START;
  flash_ = memory_->virtual_base() + FLASH_START;
  wave_ram_ = memory_->virtual_base() + WAVE_RAM_START;
  palette_ram_ = memory_->virtual_base() + PVR_PALETTE_START;
  video_ram_ = memory_->virtual_base() + PVR_VRAM32_START;
  aica_regs_ = memory_->virtual_base() + AICA_REG_START;
  ram_ = memory_->virtual_base() + MAIN_RAM_1_START;

  return true;
}
// clang-format on
