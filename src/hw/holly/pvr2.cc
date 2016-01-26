#include "core/core.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/dreamcast.h"

using namespace dvm::hw;
using namespace dvm::hw::holly;
using namespace dvm::hw::sh4;
using namespace dvm::renderer;

PVR2::PVR2(Dreamcast *dc)
    : dc_(dc), line_timer_(INVALID_TIMER), current_scanline_(0), rps_(0.0f) {}

bool PVR2::Init() {
  scheduler_ = dc_->scheduler;
  holly_ = dc_->holly;
  ta_ = dc_->ta;
  texcache_ = dc_->texcache;
  pvr_regs_ = dc_->pvr_regs;
  palette_ram_ = dc_->palette_ram;
  video_ram_ = dc_->video_ram;

// initialize registers
#define PVR_REG(addr, name, flags, default, type) \
  pvr_regs_[name##_OFFSET] = {flags, default};
#include "hw/holly/pvr2_regs.inc"
#undef PVR_REG

  ReconfigureSPG();

  return true;
}

uint32_t PVR2::ReadRegister(void *ctx, uint32_t addr) {
  PVR2 *self = reinterpret_cast<PVR2 *>(ctx);

  uint32_t offset = addr >> 2;
  Register &reg = self->pvr_regs_[offset];

  if (!(reg.flags & R)) {
    LOG_WARNING("Invalid read access at 0x%x", addr);
    return 0;
  }

  return reg.value;
}

void PVR2::WriteRegister(void *ctx, uint32_t addr, uint32_t value) {
  PVR2 *self = reinterpret_cast<PVR2 *>(ctx);

  uint32_t offset = addr >> 2;
  Register &reg = self->pvr_regs_[offset];

  if (!(reg.flags & W)) {
    LOG_WARNING("Invalid write access at 0x%x", addr);
    return;
  }

  reg.value = value;

  switch (offset) {
    case SOFTRESET_OFFSET: {
      bool reset_ta = value & 0x1;
      if (reset_ta) {
        self->ta_->SoftReset();
      }
    } break;

    case TA_LIST_INIT_OFFSET: {
      self->ta_->InitContext(self->dc_->TA_ISP_BASE.base_address);
    } break;

    case STARTRENDER_OFFSET: {
      // track render stats
      {
        auto now = std::chrono::high_resolution_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - self->last_render_);
        self->last_render_ = now;
        self->rps_ = 1000000000.0f / delta.count();
      }

      self->ta_->FinalizeContext(self->dc_->PARAM_BASE.base_address);
    } break;

    case SPG_LOAD_OFFSET:
    case FB_R_CTRL_OFFSET: {
      self->ReconfigureSPG();
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

template uint8_t PVR2::ReadVRamInterleaved(void *ctx, uint32_t addr);
template uint16_t PVR2::ReadVRamInterleaved(void *ctx, uint32_t addr);
template uint32_t PVR2::ReadVRamInterleaved(void *ctx, uint32_t addr);
template <typename T>
T PVR2::ReadVRamInterleaved(void *ctx, uint32_t addr) {
  PVR2 *self = reinterpret_cast<PVR2 *>(ctx);

  addr = MAP64(addr);

  return dvm::load<T>(&self->video_ram_[addr]);
}

template void PVR2::WriteVRamInterleaved(void *ctx, uint32_t addr,
                                         uint16_t value);
template void PVR2::WriteVRamInterleaved(void *ctx, uint32_t addr,
                                         uint32_t value);
template <typename T>
void PVR2::WriteVRamInterleaved(void *ctx, uint32_t addr, T value) {
  PVR2 *self = reinterpret_cast<PVR2 *>(ctx);

  addr = MAP64(addr);

  dvm::store(&self->video_ram_[addr], value);
}

void PVR2::ReconfigureSPG() {
  // get and scale pixel clock frequency
  int pixel_clock = 13500000;
  if (dc_->FB_R_CTRL.vclk_div) {
    pixel_clock *= 2;
  }

  // hcount is number of pixel clock cycles per line - 1
  int line_clock = pixel_clock / (dc_->SPG_LOAD.hcount + 1);
  if (dc_->SPG_CONTROL.interlace) {
    line_clock *= 2;
  }

  LOG_INFO(
      "ReconfigureSPG: pixel_clock %d, line_clock %d, vcount %d, hcount %d, "
      "interlace %d, vbstart %d, vbend %d",
      pixel_clock, line_clock, dc_->SPG_LOAD.vcount, dc_->SPG_LOAD.hcount,
      dc_->SPG_CONTROL.interlace, dc_->SPG_VBLANK.vbstart,
      dc_->SPG_VBLANK.vbend);

  if (line_timer_ != INVALID_TIMER) {
    scheduler_->RemoveTimer(line_timer_);
    line_timer_ = INVALID_TIMER;
  }

  line_timer_ = scheduler_->AddTimer(HZ_TO_NANO(line_clock),
                                     std::bind(&PVR2::NextScanline, this));
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
}
