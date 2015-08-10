#include "holly/holly.h"
#include "holly/pvr2.h"

using namespace dreavm::cpu;
using namespace dreavm::emu;
using namespace dreavm::holly;
using namespace dreavm::renderer;

PVR2::PVR2(Scheduler &scheduler, Memory &memory, Holly &holly)
    : scheduler_(scheduler),
      memory_(memory),
      holly_(holly),
      ta_(memory, holly, *this),
      rb_(nullptr),
      line_timer_(INVALID_HANDLE),
      current_scanline_(0),
      fps_(0),
      vbps_(0) {
  vram_ = new uint8_t [PVR_VRAM32_SIZE];
  pram_ = new uint8_t [PVR_PALETTE_SIZE];
}

PVR2::~PVR2() {
  delete[] vram_;
  delete[] pram_;
}

bool PVR2::Init(Backend *rb) {
  rb_ = rb;

  InitMemory();

  if (!ta_.Init(rb_)) {
    return false;
  }

  ResetState();
  ReconfigureVideoOutput();
  ReconfigureSPG();

  return true;
}

void PVR2::ToggleTracing() { ta_.ToggleTracing(); }

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
T PVR2::ReadInterleaved(void *ctx, uint32_t addr) {
  PVR2 *pvr = reinterpret_cast<PVR2 *>(ctx);
  addr = MAP64(addr);
  return *reinterpret_cast<T *>(&pvr->vram_[addr]);
}

template <typename T>
void PVR2::WriteInterleaved(void *ctx, uint32_t addr, T value) {
  PVR2 *pvr = reinterpret_cast<PVR2 *>(ctx);
  addr = MAP64(addr);
  *reinterpret_cast<T *>(&pvr->vram_[addr]) = value;
}

uint32_t PVR2::ReadRegister(void *ctx, uint32_t addr) {
  PVR2 *pvr = (PVR2 *)ctx;
  Register &reg = pvr->regs_[addr >> 2];

  if (!(reg.flags & R)) {
    LOG(WARNING) << "Invalid read access at 0x" << std::hex << addr;
    return 0;
  }

  return reg.value;
}

void PVR2::WriteRegister(void *ctx, uint32_t addr, uint32_t value) {
  PVR2 *pvr = (PVR2 *)ctx;
  Register &reg = pvr->regs_[addr >> 2];

  if (!(reg.flags & W)) {
    LOG(WARNING) << "Invalid write access at 0x" << std::hex << addr;
    return;
  }

  reg.value = value;

  if (reg.offset == SOFTRESET_OFFSET) {
    bool reset_ta = value & 0x1;
    if (reset_ta) {
      pvr->ta_.SoftReset();
    }
  } else if (reg.offset == STARTRENDER_OFFSET) {
    {
      auto now = std::chrono::high_resolution_clock::now();
      auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
          now - pvr->last_frame_);
      pvr->last_frame_ = now;
      pvr->fps_ = 1000000000.0f / delta.count();
    }

    pvr->ta_.RenderContext(pvr->PARAM_BASE.base_address);
  } else if (reg.offset == SPG_CONTROL_OFFSET) {
    pvr->ReconfigureVideoOutput();
  } else if (reg.offset == SPG_LOAD_OFFSET || reg.offset == FB_R_CTRL_OFFSET) {
    pvr->ReconfigureSPG();
  } else if (reg.offset == TA_LIST_INIT_OFFSET) {
    pvr->ta_.InitContext(pvr->TA_ISP_BASE.base_address);
  }
}

void PVR2::InitMemory() {
  memory_.Mount(PVR_VRAM32_START, PVR_VRAM32_END, MIRROR_MASK, vram_);
  memory_.Handle(PVR_VRAM64_START, PVR_VRAM64_END, MIRROR_MASK, this,
                 &PVR2::ReadInterleaved<uint8_t>,
                 &PVR2::ReadInterleaved<uint16_t>,
                 &PVR2::ReadInterleaved<uint32_t>, nullptr, nullptr,
                 &PVR2::WriteInterleaved<uint16_t>,
                 &PVR2::WriteInterleaved<uint32_t>, nullptr);

  memory_.Handle(PVR_REG_START, PVR_REG_END, MIRROR_MASK, this,
                 &PVR2::ReadRegister, &PVR2::WriteRegister);

  memory_.Mount(PVR_PALETTE_START, PVR_PALETTE_END, MIRROR_MASK, pram_);
}

void PVR2::ResetState() {
  memset(vram_, 0, PVR_VRAM32_SIZE);
  memset(pram_, 0, PVR_PALETTE_SIZE);

// initialize registers
#define PVR_REG(addr, name, flags, default, type) \
  regs_[name##_OFFSET >> 2] = {name##_OFFSET, flags, default};
#include "holly/pvr2_regs.inc"
#undef PVR_REG
}

void PVR2::ReconfigureVideoOutput() {
  int render_width = 320;
  int render_height = 240;

  // interlaced and VGA mode both render at full resolution
  if (SPG_CONTROL.interlace || (!SPG_CONTROL.NTSC && !SPG_CONTROL.PAL)) {
    render_width = 640;
    render_height = 480;
  }

  LOG(INFO) << "ReconfigureVideoOutput width " << render_width << ", height "
            << render_height << ", interlace " << SPG_CONTROL.interlace
            << ", NTSC " << SPG_CONTROL.NTSC << ", PAL " << SPG_CONTROL.PAL;

  ta_.ResizeVideo(render_width, render_height);
}

void PVR2::ReconfigureSPG() {
  static const int PIXEL_CLOCK = 27000000;  // 27mhz

  // FIXME I don't understand vcount here
  // vcount
  // Specify "number of lines per field - 1" for the CRT; in interlace mode,
  // specify "number of lines per field/2 - 1." (default = 0x106)
  // PAL interlaced = vcount 624, vbstart 620, vbend 44. why isn't vcount ~200?
  // VGA non-interlaced = vcount 524, vbstart 520, vbend 40
  int pixel_clock = FB_R_CTRL.vclk_div ? PIXEL_CLOCK : (PIXEL_CLOCK / 2);
  int line_clock = pixel_clock / (SPG_LOAD.hcount + 1);

  // HACK seems to get interlaced mode to vsync reasonably
  if (SPG_CONTROL.interlace) {
    line_clock *= 2;
  }

  LOG(INFO) << "ReconfigureSPG: pixel_clock " << pixel_clock << ", line_clock "
            << line_clock << ", vcount " << SPG_LOAD.vcount << ", hcount "
            << SPG_LOAD.hcount << ", interlace " << SPG_CONTROL.interlace
            << ", vbstart " << SPG_VBLANK.vbstart << ", vbend "
            << SPG_VBLANK.vbend;

  if (line_timer_ != INVALID_HANDLE) {
    scheduler_.RemoveTimer(line_timer_);
    line_timer_ = INVALID_HANDLE;
  }

  line_timer_ = scheduler_.AddTimer(HZ_TO_NANO(line_clock),
                                    std::bind(&PVR2::LineClockUpdate, this));
}

void PVR2::LineClockUpdate() {
  int num_scanlines = SPG_LOAD.vcount + 1;
  if (current_scanline_ > num_scanlines) {
    current_scanline_ = 0;
  }

  // vblank in
  if (current_scanline_ == SPG_VBLANK_INT.vblank_in_line_number) {
    holly_.RequestInterrupt(HOLLY_INTC_PCVIINT);
  }

  // vblank out
  if (current_scanline_ == SPG_VBLANK_INT.vblank_out_line_number) {
    holly_.RequestInterrupt(HOLLY_INTC_PCVOINT);
  }

  // hblank in
  holly_.RequestInterrupt(HOLLY_INTC_PCHIINT);

  bool was_vsync = SPG_STATUS.vsync;
  SPG_STATUS.vsync = SPG_VBLANK.vbstart < SPG_VBLANK.vbend
                         ? (current_scanline_ >= SPG_VBLANK.vbstart &&
                            current_scanline_ < SPG_VBLANK.vbend)
                         : (current_scanline_ >= SPG_VBLANK.vbstart ||
                            current_scanline_ < SPG_VBLANK.vbend);
  SPG_STATUS.scanline = current_scanline_++;

  if (!was_vsync && SPG_STATUS.vsync) {
    // track vblank stats
    auto now = std::chrono::high_resolution_clock::now();
    auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - last_vblank_);
    last_vblank_ = now;
    vbps_ = 1000000000.0f / delta.count();

    // FIXME toggle SPG_STATUS.fieldnum on vblank?
  }
}
