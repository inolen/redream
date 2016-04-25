#include "core/memory.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/pvr2_types.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

using namespace re::hw;
using namespace re::hw::holly;
using namespace re::hw::sh4;
using namespace re::renderer;

// clang-format off
AM_BEGIN(PVR2, reg_map)
  AM_RANGE(0x00000000, 0x00000fff) AM_HANDLE(nullptr,
                                             nullptr,
                                             &PVR2::ReadRegister,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             &PVR2::WriteRegister,
                                             nullptr)
  AM_RANGE(0x00001000, 0x00001fff) AM_MOUNT()
AM_END()

AM_BEGIN(PVR2, vram_map)
  AM_RANGE(0x00000000, 0x007fffff) AM_MOUNT()
  AM_RANGE(0x01000000, 0x017fffff) AM_HANDLE(&PVR2::ReadVRamInterleaved<uint8_t>,
                                             &PVR2::ReadVRamInterleaved<uint16_t>,
                                             &PVR2::ReadVRamInterleaved<uint32_t>,
                                             nullptr,
                                             &PVR2::WriteVRamInterleaved<uint8_t>,
                                             &PVR2::WriteVRamInterleaved<uint16_t>,
                                             &PVR2::WriteVRamInterleaved<uint32_t>,
                                             nullptr)
AM_END()
    // clang-format on

    PVR2::PVR2(Dreamcast &dc)
    : Device(dc, "pvr"),
      dc_(dc),
      scheduler_(nullptr),
      holly_(nullptr),
      ta_(nullptr),
      palette_ram_(nullptr),
      video_ram_(nullptr),
      regs_(),
      line_timer_(INVALID_TIMER),
      line_clock_(0),
      current_scanline_(0) {}

bool PVR2::Init() {
  scheduler_ = dc_.scheduler();
  holly_ = dc_.holly();
  ta_ = dc_.ta();
  palette_ram_ = dc_.sh4()->space().Translate(0x005f9000);
  video_ram_ = dc_.sh4()->space().Translate(0x04000000);

// initialize registers
#define PVR_REG(addr, name, flags, default, type) \
  regs_[name##_OFFSET] = {flags, default};
#define PVR_REG_R32(name) \
  regs_[name##_OFFSET].read = make_delegate(&PVR2::name##_r, this)
#define PVR_REG_W32(name) \
  regs_[name##_OFFSET].write = make_delegate(&PVR2::name##_w, this)
#include "hw/holly/pvr2_regs.inc"
  PVR_REG_W32(SPG_LOAD);
  PVR_REG_W32(FB_R_CTRL);
#undef PVR_REG

  // configure initial vsync interval
  ReconfigureSPG();

  return true;
}

uint32_t PVR2::ReadRegister(uint32_t addr) {
  uint32_t offset = addr >> 2;
  Register &reg = regs_[offset];

  if (!(reg.flags & R)) {
    LOG_WARNING("Invalid read access at 0x%x", addr);
    return 0;
  }

  if (reg.read) {
    return reg.read(reg);
  }

  return reg.value;
}

void PVR2::WriteRegister(uint32_t addr, uint32_t value) {
  uint32_t offset = addr >> 2;
  Register &reg = regs_[offset];

  // // forward some reads and writes to the TA
  // if (offset >= TA_OL_BASE && offset <= TA_NEXT_OPB_INIT) {
  //   ta_->ReadRegister(addr);
  //   return;
  // }

  if (!(reg.flags & W)) {
    LOG_WARNING("Invalid write access at 0x%x", addr);
    return;
  }

  uint32_t old_value = reg.value;
  reg.value = value;

  if (reg.write) {
    reg.write(reg, old_value);
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
  if (FB_R_CTRL.vclk_div) {
    pixel_clock *= 2;
  }

  // hcount is number of pixel clock cycles per line - 1
  line_clock_ = pixel_clock / (SPG_LOAD.hcount + 1);
  if (SPG_CONTROL.interlace) {
    line_clock_ *= 2;
  }

  LOG_INFO(
      "ReconfigureSPG: pixel_clock %d, line_clock %d, vcount %d, hcount %d, "
      "interlace %d, vbstart %d, vbend %d",
      pixel_clock, line_clock_, SPG_LOAD.vcount, SPG_LOAD.hcount,
      SPG_CONTROL.interlace, SPG_VBLANK.vbstart, SPG_VBLANK.vbend);

  if (line_timer_ != INVALID_TIMER) {
    scheduler_->CancelTimer(line_timer_);
    line_timer_ = nullptr;
  }

  line_timer_ = scheduler_->ScheduleTimer(
      re::make_delegate(&PVR2::NextScanline, this), HZ_TO_NANO(line_clock_));
}

void PVR2::NextScanline() {
  uint32_t num_scanlines = SPG_LOAD.vcount + 1;
  if (current_scanline_ > num_scanlines) {
    current_scanline_ = 0;
  }

  // vblank in
  if (current_scanline_ == SPG_VBLANK_INT.vblank_in_line_number) {
    holly_->RequestInterrupt(HOLLY_INTC_PCVIINT);
  }

  // vblank out
  if (current_scanline_ == SPG_VBLANK_INT.vblank_out_line_number) {
    holly_->RequestInterrupt(HOLLY_INTC_PCVOINT);
  }

  // hblank in
  holly_->RequestInterrupt(HOLLY_INTC_PCHIINT);

  // bool was_vsync = SPG_STATUS.vsync;
  SPG_STATUS.vsync = SPG_VBLANK.vbstart < SPG_VBLANK.vbend
                         ? (current_scanline_ >= SPG_VBLANK.vbstart &&
                            current_scanline_ < SPG_VBLANK.vbend)
                         : (current_scanline_ >= SPG_VBLANK.vbstart ||
                            current_scanline_ < SPG_VBLANK.vbend);
  SPG_STATUS.scanline = current_scanline_++;

  // FIXME toggle SPG_STATUS.fieldnum on vblank?
  // if (!was_vsync && SPG_STATUS.vsync) {
  // }

  // reschedule
  line_timer_ = scheduler_->ScheduleTimer(
      re::make_delegate(&PVR2::NextScanline, this), HZ_TO_NANO(line_clock_));
}

W32_DELEGATE(PVR2::SPG_LOAD) { ReconfigureSPG(); }

W32_DELEGATE(PVR2::FB_R_CTRL) { ReconfigureSPG(); }
