#include "core/core.h"
#include "emu/dreamcast.h"

using namespace dreavm::cpu;
using namespace dreavm::emu;
using namespace dreavm::holly;
using namespace dreavm::renderer;

PVR2::PVR2(Dreamcast *dc)
    : dc_(dc),
      line_timer_(INVALID_HANDLE),
      current_scanline_(0),
      fps_(0),
      vbps_(0) {}

void PVR2::Init() {
  scheduler_ = dc_->scheduler();
  holly_ = dc_->holly();
  ta_ = dc_->ta();
  pvr_regs_ = dc_->pvr_regs();
  video_ram_ = dc_->video_ram();

  ReconfigureSPG();
}

uint32_t PVR2::ReadRegister32(uint32_t addr) {
  uint32_t offset = addr >> 2;
  Register &reg = pvr_regs_[offset];

  if (!(reg.flags & R)) {
    LOG_WARNING("Invalid read access at 0x%x", addr);
    return 0;
  }

  return reg.value;
}

void PVR2::WriteRegister32(uint32_t addr, uint32_t value) {
  uint32_t offset = addr >> 2;
  Register &reg = pvr_regs_[offset];

  if (!(reg.flags & W)) {
    LOG_WARNING("Invalid write access at 0x%x", addr);
    return;
  }

  reg.value = value;

  switch (offset) {
    case SOFTRESET_OFFSET: {
      bool reset_ta = value & 0x1;
      if (reset_ta) {
        ta_->SoftReset();
      }
    } break;

    case TA_LIST_INIT_OFFSET: {
      ta_->InitContext(dc_->TA_ISP_BASE.base_address);
    } break;

    case STARTRENDER_OFFSET: {
      {
        auto now = std::chrono::high_resolution_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - last_frame_);
        last_frame_ = now;
        fps_ = 1000000000.0f / delta.count();
      }

      ta_->SaveLastContext(dc_->PARAM_BASE.base_address);
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

uint8_t PVR2::ReadInterleaved8(uint32_t addr) {
  addr = MAP64(addr);
  return *reinterpret_cast<uint8_t *>(&video_ram_[addr]);
}

uint16_t PVR2::ReadInterleaved16(uint32_t addr) {
  addr = MAP64(addr);
  return *reinterpret_cast<uint16_t *>(&video_ram_[addr]);
}

uint32_t PVR2::ReadInterleaved32(uint32_t addr) {
  addr = MAP64(addr);
  return *reinterpret_cast<uint32_t *>(&video_ram_[addr]);
}

void PVR2::WriteInterleaved16(uint32_t addr, uint16_t value) {
  addr = MAP64(addr);
  *reinterpret_cast<uint16_t *>(&video_ram_[addr]) = value;
}

void PVR2::WriteInterleaved32(uint32_t addr, uint32_t value) {
  addr = MAP64(addr);
  *reinterpret_cast<uint32_t *>(&video_ram_[addr]) = value;
}

void PVR2::ReconfigureSPG() {
  static const int PIXEL_CLOCK = 27000000;  // 27mhz

  // FIXME I don't understand vcount here
  // vcount
  // Specify "number of lines per field - 1" for the CRT; in interlace mode,
  // specify "number of lines per field/2 - 1." (default = 0x106)
  // PAL interlaced = vcount 624, vbstart 620, vbend 44. why isn't vcount ~200?
  // VGA non-interlaced = vcount 524, vbstart 520, vbend 40
  int pixel_clock = dc_->FB_R_CTRL.vclk_div ? PIXEL_CLOCK : (PIXEL_CLOCK / 2);
  int line_clock = pixel_clock / (dc_->SPG_LOAD.hcount + 1);

  // HACK seems to get interlaced mode to vsync reasonably
  if (dc_->SPG_CONTROL.interlace) {
    line_clock *= 2;
  }

  LOG_INFO(
      "ReconfigureSPG: pixel_clock %d, line_clock %d, vcount %d, hcount %d, "
      "interlace %d, vbstart %d, vbend %d",
      pixel_clock, line_clock, dc_->SPG_LOAD.vcount, dc_->SPG_LOAD.hcount,
      dc_->SPG_CONTROL.interlace, dc_->SPG_VBLANK.vbstart,
      dc_->SPG_VBLANK.vbend);

  if (line_timer_ != INVALID_HANDLE) {
    scheduler_->RemoveTimer(line_timer_);
    line_timer_ = INVALID_HANDLE;
  }

  line_timer_ = scheduler_->AddTimer(HZ_TO_NANO(line_clock),
                                     std::bind(&PVR2::LineClockUpdate, this));
}

void PVR2::LineClockUpdate() {
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

  bool was_vsync = dc_->SPG_STATUS.vsync;
  dc_->SPG_STATUS.vsync = dc_->SPG_VBLANK.vbstart < dc_->SPG_VBLANK.vbend
                              ? (current_scanline_ >= dc_->SPG_VBLANK.vbstart &&
                                 current_scanline_ < dc_->SPG_VBLANK.vbend)
                              : (current_scanline_ >= dc_->SPG_VBLANK.vbstart ||
                                 current_scanline_ < dc_->SPG_VBLANK.vbend);
  dc_->SPG_STATUS.scanline = current_scanline_++;

  if (!was_vsync && dc_->SPG_STATUS.vsync) {
    // track vblank stats
    auto now = std::chrono::high_resolution_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - last_vblank_);
    last_vblank_ = now;
    vbps_ = 1000000000.0f / delta.count();

    // FIXME toggle SPG_STATUS.fieldnum on vblank?
  }
}
