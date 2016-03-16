#include "core/memory.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

using namespace re::hw;
using namespace re::hw::holly;
using namespace re::hw::sh4;
using namespace re::renderer;

PVR2::PVR2(Dreamcast *dc)
    : Device(*dc),
      MemoryInterface(this),
      dc_(dc),
      line_timer_(INVALID_TIMER),
      current_scanline_(0),
      rps_(0.0f) {}

bool PVR2::Init() {
  scheduler_ = dc_->scheduler;
  holly_ = dc_->holly;
  ta_ = dc_->ta;
  pvr_regs_ = dc_->pvr_regs;
  palette_ram_ = dc_->memory->TranslateVirtual(PVR_PALETTE_START);
  video_ram_ = dc_->memory->TranslateVirtual(PVR_VRAM32_START);

// initialize registers
#define PVR_REG(addr, name, flags, default, type) \
  pvr_regs_[name##_OFFSET] = {flags, default};
#include "hw/holly/pvr2_regs.inc"
#undef PVR_REG

  // configure initial vsync interval
  ReconfigureSPG();

  return true;
}

void PVR2::MapPhysicalMemory(Memory &memory, MemoryMap &memmap) {
  RegionHandle pvr_reg_handle = memory.AllocRegion(
      PVR_REG_START, PVR_REG_SIZE, nullptr, nullptr,
      make_delegate(&PVR2::ReadRegister, this), nullptr, nullptr, nullptr,
      make_delegate(&PVR2::WriteRegister, this), nullptr);

  RegionHandle pvr_vram64_handle = memory.AllocRegion(
      PVR_VRAM64_START, PVR_VRAM64_SIZE,
      make_delegate(&PVR2::ReadVRamInterleaved<uint8_t>, this),
      make_delegate(&PVR2::ReadVRamInterleaved<uint16_t>, this),
      make_delegate(&PVR2::ReadVRamInterleaved<uint32_t>, this), nullptr,
      nullptr, make_delegate(&PVR2::WriteVRamInterleaved<uint16_t>, this),
      make_delegate(&PVR2::WriteVRamInterleaved<uint32_t>, this), nullptr);

  memmap.Mount(pvr_reg_handle, PVR_REG_SIZE, PVR_REG_START);
  memmap.Mount(pvr_vram64_handle, PVR_VRAM64_SIZE, PVR_VRAM64_START);
}

uint32_t PVR2::ReadRegister(uint32_t addr) {
  uint32_t offset = addr >> 2;
  Register &reg = pvr_regs_[offset];

  if (!(reg.flags & R)) {
    LOG_WARNING("Invalid read access at 0x%x", addr);
    return 0;
  }

  return reg.value;
}

void PVR2::WriteRegister(uint32_t addr, uint32_t value) {
  uint32_t offset = addr >> 2;
  Register &reg = pvr_regs_[offset];

  if (!(reg.flags & W)) {
    LOG_WARNING("Invalid write access at 0x%x", addr);
    return;
  }

  reg.value = value;

  switch (offset) {
    case SOFTRESET_OFFSET: {
      if (value & 0x1) {
        ta_->SoftReset();
      }
    } break;

    case TA_LIST_INIT_OFFSET: {
      if (value & 0x80000000) {
        ta_->InitContext(dc_->TA_ISP_BASE.base_address);
      }
    } break;

    case TA_LIST_CONT_OFFSET: {
      if (value & 0x80000000) {
        LOG_WARNING("Unsupported TA_LIST_CONT");
      }
    } break;

    case STARTRENDER_OFFSET: {
      if (value) {
        // track render stats
        {
          auto now = std::chrono::high_resolution_clock::now();
          auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
              now - last_render_);
          last_render_ = now;
          rps_ = 1000000000.0f / delta.count();
        }

        ta_->FinalizeContext(dc_->PARAM_BASE.base_address);
      }
    } break;

    case SPG_LOAD_OFFSET:
    case FB_R_CTRL_OFFSET: {
      ReconfigureSPG();
    } break;
  }
}

// the dreamcast has 8MB of vram, split into two 4MB banks, with two ways of
// accessing it:
// 0x04000000 -> 0x047fffff, 32-bit sequential access
// 0x05000000 -> 0x057fffff, 64-bit interleaved access
//
// in 64-bit interleaved mode, the addresses map like so:
// 0x05000000 = 0x0400000
// 0x05400000 = 0x0400004
// 0x05400002 = 0x0400006
// 0x05000004 = 0x0400008
// 0x05000006 = 0x040000a
// 0x05400004 = 0x040000c
// 0x05000008 = 0x0400010
// 0x05400008 = 0x0400014
// 0x0500000c = 0x0400018
// 0x0540000c = 0x040001c
static uint32_t MAP64(uint32_t addr) {
  return (((addr & 0x003ffffc) << 1) + ((addr & 0x00400000) >> 20) +
          (addr & 0x3));
}

template <typename T>
T PVR2::ReadVRamInterleaved(uint32_t addr) {
  addr = MAP64(addr);
  return re::load<T>(&video_ram_[addr]);
}

template <typename T>
void PVR2::WriteVRamInterleaved(uint32_t addr, T value) {
  addr = MAP64(addr);
  re::store(&video_ram_[addr], value);
}

void PVR2::ReconfigureSPG() {
  // get and scale pixel clock frequency
  int pixel_clock = 13500000;
  if (dc_->FB_R_CTRL.vclk_div) {
    pixel_clock *= 2;
  }

  // hcount is number of pixel clock cycles per line - 1
  line_clock_ = pixel_clock / (dc_->SPG_LOAD.hcount + 1);
  if (dc_->SPG_CONTROL.interlace) {
    line_clock_ *= 2;
  }

  LOG_INFO(
      "ReconfigureSPG: pixel_clock %d, line_clock %d, vcount %d, hcount %d, "
      "interlace %d, vbstart %d, vbend %d",
      pixel_clock, line_clock_, dc_->SPG_LOAD.vcount, dc_->SPG_LOAD.hcount,
      dc_->SPG_CONTROL.interlace, dc_->SPG_VBLANK.vbstart,
      dc_->SPG_VBLANK.vbend);

  if (line_timer_ != INVALID_TIMER) {
    scheduler_->CancelTimer(line_timer_);
    line_timer_ = nullptr;
  }

  line_timer_ = scheduler_->ScheduleTimer(
      re::make_delegate(&PVR2::NextScanline, this), HZ_TO_NANO(line_clock_));
}

void PVR2::NextScanline() {
  uint32_t num_scanlines = dc_->SPG_LOAD.vcount + 1;
  if (current_scanline_ > num_scanlines) {
    current_scanline_ = 0;
  }

  // vblank in
  if (current_scanline_ == dc_->SPG_VBLANK_INT.vblank_in_line_number) {
    holly_->RequestInterrupt(HOLLY_INTC_PCVIINT);
  }

  // vblank out
  if (current_scanline_ == dc_->SPG_VBLANK_INT.vblank_out_line_number) {
    holly_->RequestInterrupt(HOLLY_INTC_PCVOINT);
  }

  // hblank in
  holly_->RequestInterrupt(HOLLY_INTC_PCHIINT);

  // bool was_vsync = dc_->SPG_STATUS.vsync;
  dc_->SPG_STATUS.vsync = dc_->SPG_VBLANK.vbstart < dc_->SPG_VBLANK.vbend
                              ? (current_scanline_ >= dc_->SPG_VBLANK.vbstart &&
                                 current_scanline_ < dc_->SPG_VBLANK.vbend)
                              : (current_scanline_ >= dc_->SPG_VBLANK.vbstart ||
                                 current_scanline_ < dc_->SPG_VBLANK.vbend);
  dc_->SPG_STATUS.scanline = current_scanline_++;

  // FIXME toggle SPG_STATUS.fieldnum on vblank?
  // if (!was_vsync && dc_->SPG_STATUS.vsync) {
  // }

  // reschedule
  line_timer_ = scheduler_->ScheduleTimer(
      re::make_delegate(&PVR2::NextScanline, this), HZ_TO_NANO(line_clock_));
}
