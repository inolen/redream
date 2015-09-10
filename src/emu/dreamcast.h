#ifndef DREAMCAST_H
#define DREAMCAST_H

#include <memory>
#include "hw/aica/aica.h"
#include "hw/sh4/sh4.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/maple/maple.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "jit/backend/backend.h"
#include "jit/frontend/frontend.h"
#include "jit/runtime.h"
#include "renderer/backend.h"
#include "system/system.h"
#include "trace/trace.h"

namespace dreavm {
namespace emu {

//
// memory layout
//
#define MEMORY_REGION(name, start, end) \
  name##_START = start, name##_END = end, name##_SIZE = end - start + 1

enum {
  // ignore all access modifier bits
  MIRROR_MASK = ~hw::sh4::ADDR_MASK,

  // main ram is mirrored an additional four times:
  // 0x0c000000 - 0x0cffffff
  // 0x0d000000 - 0x0dffffff
  // 0x0e000000 - 0x0effffff
  // 0x0f000000 - 0x0fffffff
  MAIN_RAM_MIRROR_MASK = MIRROR_MASK | 0x03000000,

  MEMORY_REGION(BIOS, 0x00000000, 0x001fffff),
  MEMORY_REGION(FLASH, 0x00200000, 0x0021ffff),
  MEMORY_REGION(HOLLY_REG, 0x005f6000, 0x005f7fff),
  MEMORY_REGION(MAPLE_REG, 0x005f6c00, 0x005f6fff),
  MEMORY_REGION(GDROM_REG, 0x005f7000, 0x005f77ff),
  MEMORY_REGION(PVR_REG, 0x005f8000, 0x005f8fff),
  MEMORY_REGION(PVR_PALETTE, 0x005f9000, 0x005f9fff),
  MEMORY_REGION(MODEM_REG, 0x00600000, 0x0067ffff),
  MEMORY_REGION(AICA_REG, 0x00700000, 0x00710fff),
  MEMORY_REGION(WAVE_RAM, 0x00800000, 0x009fffff),
  MEMORY_REGION(EXPDEV, 0x01000000, 0x01ffffff),
  MEMORY_REGION(PVR_VRAM32, 0x04000000, 0x047fffff),
  MEMORY_REGION(PVR_VRAM64, 0x05000000, 0x057fffff),
  MEMORY_REGION(MAIN_RAM, 0x0c000000, 0x0cffffff),
  MEMORY_REGION(TA_CMD, 0x10000000, 0x107fffff),
  MEMORY_REGION(TA_TEXTURE, 0x11000000, 0x11ffffff),
  MEMORY_REGION(UNASSIGNED, 0x14000000, 0x1bffffff),
  MEMORY_REGION(SH4_REG, 0x1c000000, 0x1fffffff),
  MEMORY_REGION(SH4_CACHE, 0x7c000000, 0x7fffffff),
  MEMORY_REGION(SH4_SQ, 0xe0000000, 0xe3ffffff)
};

//
// registers
//
enum {  //
  R = 0x1,
  W = 0x2,
  RW = 0x3,
  UNDEFINED = 0x0
};

struct Register {
  Register() : flags(RW), value(0) {}
  Register(uint8_t flags, uint32_t value) : flags(flags), value(value) {}

  uint8_t flags;
  uint32_t value;
};

enum {
#define AICA_REG(addr, name, flags, default, type) \
  name##_OFFSET = addr - emu::AICA_REG_START,
#include "hw/aica/aica_regs.inc"
#undef AICA_REG

#define HOLLY_REG(addr, name, flags, default, type) \
  name##_OFFSET = (addr - emu::HOLLY_REG_START) >> 2,
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG

#define PVR_REG(addr, name, flags, default_value, type) \
  name##_OFFSET = (addr - emu::PVR_REG_START) >> 2,
#include "hw/holly/pvr2_regs.inc"
#undef PVR_REG
};

class Dreamcast {
 public:
  hw::Memory *memory() { return memory_.get(); }
  hw::Scheduler *scheduler() { return scheduler_.get(); }
  renderer::Backend *rb() { return rb_.get(); }
  jit::Runtime *runtime() { return runtime_.get(); }
  hw::aica::AICA *aica() { return aica_.get(); }
  hw::gdrom::GDROM *gdrom() { return gdrom_.get(); }
  hw::holly::Holly *holly() { return holly_.get(); }
  hw::holly::PVR2 *pvr() { return pvr_.get(); }
  hw::holly::TileAccelerator *ta() { return ta_.get(); }
  hw::maple::Maple *maple() { return maple_.get(); }
  hw::sh4::SH4 *sh4() { return sh4_.get(); }
  trace::TraceWriter *trace_writer() { return trace_writer_.get(); }

  uint8_t *aica_regs() { return aica_regs_; }
  Register *holly_regs() { return holly_regs_; }
  Register *pvr_regs() { return pvr_regs_; }

  uint8_t *wave_ram() { return wave_ram_; }
  uint8_t *palette_ram() { return palette_ram_; }
  uint8_t *video_ram() { return video_ram_; }

  Dreamcast();

  void Run(const char *path);

#define HOLLY_REG(offset, name, flags, default, type) \
  type &name{reinterpret_cast<type &>(holly_regs_[name##_OFFSET].value)};
#include "hw/holly/holly_regs.inc"
#undef HOLLY_REG

#define PVR_REG(offset, name, flags, default, type) \
  type &name{reinterpret_cast<type &>(pvr_regs_[name##_OFFSET].value)};
#include "hw/holly/pvr2_regs.inc"
#undef PVR_REG

 private:
  bool Init();
  void InitMemory();
  void InitRegisters();

  bool LoadBios(const char *path);
  bool LoadFlash(const char *path);
  bool LaunchBIN(const char *path);
  bool LaunchGDI(const char *path);

  void PumpEvents();
  void ToggleTracing();
  void RenderFrame();

  system::System sys_;
  std::unique_ptr<hw::Memory> memory_;
  std::unique_ptr<hw::Scheduler> scheduler_;
  std::unique_ptr<renderer::Backend> rb_;
  std::unique_ptr<jit::frontend::Frontend> rt_frontend_;
  std::unique_ptr<jit::backend::Backend> rt_backend_;
  std::unique_ptr<jit::Runtime> runtime_;
  std::unique_ptr<hw::aica::AICA> aica_;
  std::unique_ptr<hw::gdrom::GDROM> gdrom_;
  std::unique_ptr<hw::holly::Holly> holly_;
  std::unique_ptr<hw::holly::PVR2> pvr_;
  std::unique_ptr<hw::holly::TileAccelerator> ta_;
  std::unique_ptr<hw::maple::Maple> maple_;
  std::unique_ptr<hw::sh4::SH4> sh4_;
  std::unique_ptr<trace::TraceWriter> trace_writer_;

  Register holly_regs_[HOLLY_REG_SIZE >> 2];
  Register pvr_regs_[PVR_REG_SIZE >> 2];

  uint8_t bios_[BIOS_SIZE];
  uint8_t flash_[FLASH_SIZE];
  uint8_t ram_[MAIN_RAM_SIZE];
  uint8_t unassigned_[UNASSIGNED_SIZE];
  uint8_t modem_mem_[MODEM_REG_SIZE];
  uint8_t aica_regs_[AICA_REG_SIZE];
  uint8_t wave_ram_[WAVE_RAM_SIZE];
  uint8_t expdev_mem_[EXPDEV_SIZE];
  uint8_t video_ram_[PVR_VRAM32_SIZE];
  uint8_t palette_ram_[PVR_PALETTE_SIZE];
};
}
}

#endif
