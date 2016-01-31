#include "hw/aica/aica.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/texture_cache.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/maple/maple.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "renderer/backend.h"
#include "trace/trace.h"

using namespace dvm;
using namespace dvm::hw;
using namespace dvm::hw::aica;
using namespace dvm::hw::gdrom;
using namespace dvm::hw::holly;
using namespace dvm::hw::maple;
using namespace dvm::hw::sh4;
using namespace dvm::jit;
using namespace dvm::renderer;
using namespace dvm::sys;
using namespace dvm::trace;

namespace dvm {
namespace hw {

// clang-format off
static bool MapMemory(Dreamcast &dc) {
  Memory *memory = dc.memory;

  if (!memory->Init()) {
    return false;
  }

  // first, allocate static regions
  RegionHandle a0_handle = memory->AllocRegion(AREA0_START, AREA0_SIZE);
  RegionHandle a1_handle = memory->AllocRegion(AREA1_START, AREA1_SIZE);
  // area 2 unused
  RegionHandle a3_handle = memory->AllocRegion(AREA3_START, AREA3_SIZE);
  // area 4 unused
  RegionHandle a5_handle = memory->AllocRegion(AREA5_START, AREA5_SIZE);
  RegionHandle a6_handle = memory->AllocRegion(AREA6_START, AREA6_SIZE);
  RegionHandle a7_handle = memory->AllocRegion(AREA7_START, AREA7_SIZE);

  // second, allocate dynamic regions that overlap static regions
  RegionHandle holly_handle = memory->AllocRegion(
    HOLLY_REG_START, HOLLY_REG_SIZE, dc.holly,
    &Holly::ReadRegister<uint8_t>,
    &Holly::ReadRegister<uint16_t>,
    &Holly::ReadRegister<uint32_t>,
    nullptr,
    &Holly::WriteRegister<uint8_t>,
    &Holly::WriteRegister<uint16_t>,
    &Holly::WriteRegister<uint32_t>,
    nullptr);
  RegionHandle pvr_reg_handle = memory->AllocRegion(
    PVR_REG_START, PVR_REG_SIZE, dc.pvr,
    nullptr,
    nullptr,
    &PVR2::ReadRegister,
    nullptr,
    nullptr,
    nullptr,
    &PVR2::WriteRegister,
    nullptr);
  // RegionHandle aica_reg_handle = memory->AllocRegion(
  //   AICA_REG_START, AICA_REG_SIZE, aica(),
  //   nullptr,
  //   nullptr,
  //   &AICA::ReadRegister,
  //   nullptr,
  //   nullptr,
  //   nullptr,
  //   &AICA::WriteRegister,
  //   nullptr);
  RegionHandle wave_ram_handle = memory->AllocRegion(
    WAVE_RAM_START, WAVE_RAM_SIZE, dc.aica,
    &AICA::ReadWave<uint8_t>,
    &AICA::ReadWave<uint16_t>,
    &AICA::ReadWave<uint32_t>,
    nullptr,
    &AICA::WriteWave<uint8_t>,
    &AICA::WriteWave<uint16_t>,
    &AICA::WriteWave<uint32_t>,
    nullptr);
  RegionHandle pvr_vram64_handle = memory->AllocRegion(
    PVR_VRAM64_START, PVR_VRAM64_SIZE, dc.pvr,
    &PVR2::ReadVRamInterleaved<uint8_t>,
    &PVR2::ReadVRamInterleaved<uint16_t>,
    &PVR2::ReadVRamInterleaved<uint32_t>,
    nullptr,
    nullptr,
    &PVR2::WriteVRamInterleaved<uint16_t>,
    &PVR2::WriteVRamInterleaved<uint32_t>,
    nullptr);
  RegionHandle ta_cmd_handle = memory->AllocRegion(
    TA_CMD_START, TA_CMD_SIZE, dc.ta,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &TileAccelerator::WriteCommand,
    nullptr);
  RegionHandle ta_texture_handle = memory->AllocRegion(
    TA_TEXTURE_START, TA_TEXTURE_SIZE, dc.ta,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    &TileAccelerator::WriteTexture,
    nullptr);
  RegionHandle sh4_reg_handle = memory->AllocRegion(
    SH4_REG_START, SH4_REG_SIZE, dc.sh4,
    &SH4::ReadRegister<uint8_t>,
    &SH4::ReadRegister<uint16_t>,
    &SH4::ReadRegister<uint32_t>,
    nullptr,
    &SH4::WriteRegister<uint8_t>,
    &SH4::WriteRegister<uint16_t>,
    &SH4::WriteRegister<uint32_t>,
    nullptr);
  RegionHandle sh4_cache_handle = memory->AllocRegion(
    SH4_CACHE_START, SH4_CACHE_SIZE, dc.sh4,
    &SH4::ReadCache<uint8_t>,
    &SH4::ReadCache<uint16_t>,
    &SH4::ReadCache<uint32_t>,
    &SH4::ReadCache<uint64_t>,
    &SH4::WriteCache<uint8_t>,
    &SH4::WriteCache<uint16_t>,
    &SH4::WriteCache<uint32_t>,
    &SH4::WriteCache<uint64_t>);
   RegionHandle sh4_sq_handle = memory->AllocRegion(
    SH4_SQ_START, SH4_SQ_SIZE, dc.sh4,
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
  // memmap.Mount(aica_reg_handle, AICA_REG_SIZE, AICA_REG_START);
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

  if (!memory->Map(memmap)) {
    return false;
  }

  dc.bios = memory->virtual_base() + BIOS_START;
  dc.flash = memory->virtual_base() + FLASH_START;
  dc.wave_ram = memory->virtual_base() + WAVE_RAM_START;
  dc.palette_ram = memory->virtual_base() + PVR_PALETTE_START;
  dc.video_ram = memory->virtual_base() + PVR_VRAM32_START;
  // dc.aica_regs = memory->virtual_base() + AICA_REG_START;
  dc.ram = memory->virtual_base() + MAIN_RAM_1_START;

  return true;
}
// clang-format on

bool CreateDreamcast(Dreamcast &dc, renderer::Backend *rb) {
  dc.scheduler = new Scheduler();
  dc.memory = new Memory();
  dc.aica = new AICA(&dc);
  dc.gdrom = new GDROM(&dc);
  dc.holly = new Holly(&dc);
  dc.maple = new Maple(&dc);
  dc.pvr = new PVR2(&dc);
  dc.sh4 = new SH4(&dc);
  dc.ta = new TileAccelerator(&dc);
  dc.texcache = new TextureCache(&dc);
  dc.rb = rb;

  if (!MapMemory(dc) ||     //
      !dc.aica->Init() ||   //
      !dc.gdrom->Init() ||  //
      !dc.holly->Init() ||  //
      !dc.maple->Init() ||  //
      !dc.pvr->Init() ||    //
      !dc.sh4->Init() ||    //
      !dc.ta->Init() ||     //
      !dc.texcache->Init()) {
    DestroyDreamcast(dc);
    return false;
  }

  return true;
}

void DestroyDreamcast(Dreamcast &dc) {
  delete dc.scheduler;
  dc.scheduler = nullptr;
  delete dc.memory;
  dc.memory = nullptr;
  delete dc.aica;
  dc.aica = nullptr;
  delete dc.gdrom;
  dc.gdrom = nullptr;
  delete dc.holly;
  dc.holly = nullptr;
  delete dc.maple;
  dc.maple = nullptr;
  delete dc.pvr;
  dc.pvr = nullptr;
  delete dc.sh4;
  dc.sh4 = nullptr;
  delete dc.ta;
  dc.ta = nullptr;
  delete dc.texcache;
  dc.texcache = nullptr;
}
}
}
