#include "holly/holly.h"
#include "holly/pixel_convert.h"
#include "holly/pvr2.h"

using namespace dreavm;
using namespace dreavm::core;
using namespace dreavm::cpu;
using namespace dreavm::emu;
using namespace dreavm::holly;
using namespace dreavm::renderer;

PVR2::PVR2(Scheduler &scheduler, Memory &memory, Holly &holly)
    : scheduler_(scheduler),
      memory_(memory),
      holly_(holly),
      rb_(nullptr),
      ta_(memory_, holly, *this),
      line_timer_(INVALID_HANDLE),
      current_scanline_(0),
      fps_(0),
      vbps_(0) {}

bool PVR2::Init(Backend *rb) {
  rb_ = rb;

  InitMemory();
  ReconfigureVideoOutput();
  ReconfigureSPG();

  if (!ta_.Init(rb)) {
    return false;
  }

  // scheduler_.AddTimer(HZ_TO_NANO(60), [&]() {
  //   rb_->BeginFrame();

  //   uint32_t framebuffer_start = VRAM_START;
  //   int pos = 0;

  //   for (int i = 0; i < 480; i++) {
  //     for (int j = 0; j < 640; j++) {
  //       uint16_t rgba = memory_.R16(framebuffer_start + pos);
  //       rb_->BitBlt(j, i, rgba);
  //       pos += 2;
  //     }
  //   }

  //   rb_->EndFrame();
  // });

  return true;
}

TextureHandle PVR2::GetTexture(TSP tsp, TCW tcw) {
  static uint8_t texture[1024 * 1024 * 4];
  static uint8_t palette[0x1000];
  static uint8_t converted[1024 * 1024 * 4];
  static uint8_t *buffer = texture;

  // TCW texture_addr field is in 64-bit units
  uint32_t texture_addr = TEXRAM_START + (tcw.texture_addr << 3);

  // see if we already have an entry
  auto it = textures_.find(texture_addr);
  if (it != textures_.end()) {
    return it->second;
  }

  // textures are either twiddled and vq compressed, twiddled and uncompressed
  // or planar
  bool twiddled = !tcw.scan_order;
  bool compressed = tcw.vq_compressed;

  // get texture dimensions
  int width = 8 << tsp.texture_u_size;
  int height = 8 << tsp.texture_v_size;
  if (!twiddled && tcw.mip_mapped) {
    height = width;
  }
  int stride = width;
  if (!twiddled && tcw.stride_select) {
    stride = TEXT_CONTROL.stride * 32;
  }

  // FIXME used for texcoords, not width / height of texture
  // if (planar && tcw.stride_select) {
  //   width = TEXT_CONTROL.stride << 5;
  // }

  // read texture
  int element_size_bits = tcw.pixel_format == TA_PIXEL_8BPP
                              ? 8
                              : tcw.pixel_format == TA_PIXEL_4BPP ? 4 : 16;
  int texture_size = (width * height * element_size_bits) >> 3;
  for (int i = 0; i < texture_size; i++) {
    texture[i] = memory_.R8(texture_addr + i);
  }

  // read palette
  if (tcw.pixel_format == TA_PIXEL_4BPP || tcw.pixel_format == TA_PIXEL_8BPP) {
    intptr_t palette_addr = 0x005f9000;
    if (tcw.pixel_format == TA_PIXEL_4BPP) {
      palette_addr |= (tcw.p.palette_selector << 4);
    } else {
      // in 8BPP palette mode, only the upper two bits are valid
      palette_addr |= ((tcw.p.palette_selector & 0x30) << 4);
    }
    for (int i = 0; i < 0x1000; i++) {
      palette[i] = memory_.R8(palette_addr + i);
    }
  }

  // we need to read the image data
  // we need to possibly look up the final rgba data indirectly if it's a
  // paletted or vq texture
  // we need to either straight convert, detwizzle or uncompress the rgba values
  // then we need to write it out as the appropriate format

  PixelFormat pixel_fmt;
  switch (tcw.pixel_format) {
    case TA_PIXEL_1555:
      buffer = converted;
      pixel_fmt = PXL_RGBA5551;
      if (compressed) {
        PixelConvert::ConvertVQ<ARGB1555, RGBA5551>(
            texture, (uint16_t *)converted, width, height);
      } else if (twiddled) {
        PixelConvert::ConvertTwiddled<ARGB1555, RGBA5551>(
            (uint16_t *)texture, (uint16_t *)converted, width, height);
      } else {
        PixelConvert::Convert<ARGB1555, RGBA5551>(
            (uint16_t *)texture, (uint16_t *)converted, stride, height);
      }
      break;

    case TA_PIXEL_565:
      buffer = converted;
      pixel_fmt = PXL_RGB565;
      if (compressed) {
        PixelConvert::ConvertVQ<RGB565, RGB565>(texture, (uint16_t *)converted,
                                                width, height);
      } else if (twiddled) {
        PixelConvert::ConvertTwiddled<RGB565, RGB565>(
            (uint16_t *)texture, (uint16_t *)converted, width, height);
      } else {
        PixelConvert::Convert<RGB565, RGB565>(
            (uint16_t *)texture, (uint16_t *)converted, stride, height);
      }
      break;

    case TA_PIXEL_4444:
      buffer = converted;
      pixel_fmt = PXL_RGBA4444;
      if (compressed) {
        PixelConvert::ConvertVQ<ARGB4444, RGBA4444>(
            texture, (uint16_t *)converted, width, height);
      } else if (twiddled) {
        PixelConvert::ConvertTwiddled<ARGB4444, RGBA4444>(
            (uint16_t *)texture, (uint16_t *)converted, width, height);
      } else {
        PixelConvert::Convert<ARGB4444, RGBA4444>(
            (uint16_t *)texture, (uint16_t *)converted, stride, height);
      }
      break;

    case TA_PIXEL_4BPP:
      buffer = converted;
      switch (PAL_RAM_CTRL.pixel_format) {
        case TA_PAL_ARGB1555:
          pixel_fmt = PXL_RGBA5551;
          LOG(FATAL) << "Unhandled";
          break;

        case TA_PAL_RGB565:
          pixel_fmt = PXL_RGB565;
          LOG(FATAL) << "Unhandled";
          break;

        case TA_PAL_ARGB4444:
          CHECK_EQ(false, twiddled);
          pixel_fmt = PXL_RGBA4444;
          PixelConvert::ConvertPal4<ARGB4444, RGBA4444>(
              texture, (uint16_t *)converted, (uint32_t *)palette, width,
              height);
          break;

        case TA_PAL_ARGB8888:
          CHECK_EQ(true, twiddled);
          pixel_fmt = PXL_RGBA8888;
          LOG(FATAL) << "Unhandled";
          break;
      }
      break;

    case TA_PIXEL_8BPP:
      buffer = converted;
      switch (PAL_RAM_CTRL.pixel_format) {
        case TA_PAL_ARGB1555:
          pixel_fmt = PXL_RGBA5551;
          LOG(FATAL) << "Unhandled";
          break;

        case TA_PAL_RGB565:
          pixel_fmt = PXL_RGB565;
          LOG(FATAL) << "Unhandled";
          break;

        case TA_PAL_ARGB4444:
          CHECK_EQ(true, twiddled);
          pixel_fmt = PXL_RGBA4444;
          PixelConvert::ConvertPal8<ARGB4444, RGBA4444>(
              texture, (uint16_t *)converted, (uint32_t *)palette, width,
              height);
          break;

        case TA_PAL_ARGB8888:
          CHECK_EQ(true, twiddled);
          pixel_fmt = PXL_RGBA8888;
          PixelConvert::ConvertPal8<ARGB8888, RGBA8888>(
              texture, (uint32_t *)converted, (uint32_t *)palette, width,
              height);
          break;
      }
      break;

    default:
      LOG(FATAL) << "Unsupported tcw pixel format " << tcw.pixel_format;
      break;
  }

  // ignore trilinear filtering for now
  FilterMode filter = tsp.filter_mode == 0 ? FILTER_NEAREST : FILTER_BILINEAR;

  TextureHandle handle =
      rb_->RegisterTexture(pixel_fmt, filter, width, height, buffer);
  if (!handle) {
    LOG(WARNING) << "Failed to register texture at 0x" << std::hex
                 << texture_addr << " (tcw 0x" << tcw.full << ")";
    return 0;
  }

  // insert into the cache
  auto result = textures_.insert(std::make_pair(texture_addr, handle));
  return result.first->second;
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
  return 0x05000000 | (((addr & 0x003ffffc) << 1) +
                       ((addr & 0x00400000) >> 20) + (addr & 0x3));
}

// template <typename T>
// T PVR2::ReadInterleaved(void *ctx, uint32_t addr) {
//   PVR2 *pvr = reinterpret_cast<PVR2 *>(ctx);
//   return pvr->vram_[MAP64(addr)]);
// }

// template <typename T>
// void PVR2::WriteInterleaved(void *ctx, uint32_t addr, T value) {
//   PVR2 *pvr = reinterpret_cast<PVR2 *>(ctx);
//   *reinterpret_cast<T *>(&pvr->vram_[MAP64(addr)]) = value;
// }

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

    pvr->ta_.StartRender(pvr->PARAM_BASE.base_address);
  } else if (reg.offset == SPG_CONTROL_OFFSET) {
    pvr->ReconfigureVideoOutput();
  } else if (reg.offset == SPG_LOAD_OFFSET || reg.offset == FB_R_CTRL_OFFSET) {
    pvr->ReconfigureSPG();
  } else if (reg.offset == TA_LIST_INIT_OFFSET) {
    pvr->ta_.InitContext(pvr->TA_ISP_BASE.base_address);
  }
}

void PVR2::InitMemory() {
  // memory_.Handle(VRAM64_BASE, VRAM64_BASE + VRAM_SIZE - 1, 0xe0000000, this,
  //               &PVR2::ReadInterleaved<uint8_t>,
  //               &PVR2::ReadInterleaved<uint16_t>,
  //               &PVR2::ReadInterleaved<uint32_t>, nullptr, nullptr,
  //               &PVR2::WriteInterleaved<uint16_t>,
  //               &PVR2::WriteInterleaved<uint32_t>, nullptr);
  memory_.Handle(PVR_REG_BASE, PVR_REG_BASE + PVR_REG_SIZE - 1, 0xe0000000,
                 this, &PVR2::ReadRegister, &PVR2::WriteRegister);

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

  rb_->ResizeFramebuffer(FB_TILE_ACELLERATOR, render_width, render_height);
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
