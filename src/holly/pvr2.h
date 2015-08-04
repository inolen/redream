#ifndef PVR_CLX2_H
#define PVR_CLX2_H

#include <chrono>
#include "emu/memory.h"
#include "emu/scheduler.h"
#include "holly/register.h"
#include "holly/tile_accelerator.h"
#include "renderer/backend.h"

namespace dreavm {
namespace holly {

class Holly;

enum {
  VRAM32_BASE = 0x04000000,
  VRAM64_BASE = 0x05000000,
  VRAM_SIZE = 0x800000,
  PVR_REG_BASE = 0x005f8000,
  PVR_REG_SIZE = 0x1000,
  PVR_PAL_BASE = 0x005f9000,
  PVR_PAL_SIZE = 0x1000,
  PIXEL_CLOCK = 27000000,  // 27mhz
#define PVR_REG(addr, name, flags, default_value, type) \
  name##_OFFSET = addr - PVR_REG_BASE,
#include "holly/pvr2_regs.inc"
#undef PVR_REG
};

union PARAM_BASE_T {
  struct {
    uint32_t base_address : 24;
    uint32_t reserved : 8;
  };
  uint32_t full;
};

union FB_R_CTRL_T {
  struct {
    uint32_t fb_enable : 1;
    uint32_t fb_line_double : 1;
    uint32_t fb_depth : 2;
    uint32_t fb_concat : 3;
    uint32_t reserved0 : 1;
    uint32_t fb_chrome_threshhold : 8;
    uint32_t fb_stripsize : 6;
    uint32_t fb_strip_buf_en : 1;
    uint32_t vclk_div : 1;
    uint32_t reserved1 : 8;
  };
  uint32_t full;
};

union FB_W_CTRL_T {
  struct {
    uint32_t fb_packmode : 3;
    uint32_t fb_dither : 1;
    uint32_t reserved0 : 4;
    uint32_t fb_kval : 8;
    uint32_t fb_alpha_threshhold : 8;
    uint32_t reserved1 : 8;
  };
  uint32_t full;
};

union FPU_SHAD_SCALE_T {
  struct {
    uint32_t scale_factor : 8;
    uint32_t intensity_volume_mode : 1;
    uint32_t reserved : 23;
  };
  uint32_t full;
};

union FPU_PARAM_CFG_T {
  struct {
    uint32_t first_ptr_burst_size : 4;
    uint32_t ptr_burst_size : 4;
    uint32_t isp_burst_threshold : 6;
    uint32_t tsp_burst_threshold : 6;
    uint32_t reserved : 1;
    uint32_t region_header_type : 1;
    uint32_t reserved1 : 10;
  };
  uint32_t full;
};

union ISP_BACKGND_T_T {
  struct {
    uint32_t tag_offset : 3;
    uint32_t tag_address : 21;
    uint32_t skip : 3;
    uint32_t shadow : 1;
    uint32_t cache_bypass : 1;
  };
  uint32_t full;
};

union ISP_FEED_CFG_T {
  struct {
    uint32_t presort : 1;
    uint32_t reserved : 2;
    uint32_t discard : 1;
    uint32_t punch_size : 10;
    uint32_t cache_size : 10;
    uint32_t reserved1 : 8;
  };
  uint32_t full;
};

union SPG_HBLANK_INT_T {
  struct {
    uint32_t line_comp_val : 10;
    uint32_t reserved : 2;
    uint32_t hblank_int_mode : 2;
    uint32_t reserved2 : 2;
    uint32_t hblank_in_interrupt : 10;
    uint32_t reserved3 : 6;
  };
  uint32_t full;
};

union SPG_VBLANK_INT_T {
  struct {
    uint32_t vblank_in_line_number : 10;
    uint32_t reserved : 6;
    uint32_t vblank_out_line_number : 10;
    uint32_t reserved2 : 6;
  };
  uint32_t full;
};

union SPG_CONTROL_T {
  struct {
    uint32_t mhsync_pol : 1;
    uint32_t mvsync_pol : 1;
    uint32_t mcsync_pol : 1;
    uint32_t spg_lock : 1;
    uint32_t interlace : 1;
    uint32_t force_field2 : 1;
    uint32_t NTSC : 1;
    uint32_t PAL : 1;
    uint32_t sync_direction : 1;
    uint32_t csync_on_h : 1;
    uint32_t reserved : 22;
  };
  uint32_t full;
};

union SPG_LOAD_T {
  struct {
    uint32_t hcount : 10;
    uint32_t reserved : 6;
    uint32_t vcount : 10;
    uint32_t reserved2 : 6;
  };
  uint32_t full;
};

union SPG_VBLANK_T {
  struct {
    uint32_t vbstart : 10;
    uint32_t reserved : 6;
    uint32_t vbend : 10;
    uint32_t reserved2 : 6;
  };
  uint32_t full;
};

union TEXT_CONTROL_T {
  struct {
    uint32_t stride : 5;
    uint32_t reserved : 3;
    uint32_t bankbit : 5;
    uint32_t reserved2 : 3;
    uint32_t index_endian : 1;
    uint32_t codebook_endian : 1;
    uint32_t reserved3 : 14;
  };
  uint32_t full;
};

union PAL_RAM_CTRL_T {
  struct {
    uint32_t pixel_format : 2;
    uint32_t reserved0 : 30;
  };
  uint32_t full;
};

union SPG_STATUS_T {
  struct {
    uint32_t scanline : 10;
    uint32_t fieldnum : 1;
    uint32_t blank : 1;
    uint32_t hsync : 1;
    uint32_t vsync : 1;
    uint32_t reserved : 18;
  };
  uint32_t full;
};

union TA_ISP_BASE_T {
  struct {
    uint32_t base_address : 24;
    uint32_t reserved : 8;
  };
  uint32_t full;
};

class PVR2 {
  friend class TileAccelerator;
  friend class TileTextureCache;

 public:
  PVR2(emu::Scheduler &scheduler, emu::Memory &memory, Holly &holly);
  ~PVR2();

  float fps() { return fps_; }
  float vbps() { return vbps_; }

  bool Init(renderer::Backend *rb);
  void ToggleTracing();

 private:
  template <typename T>
  static T ReadInterleaved(void *ctx, uint32_t addr);
  template <typename T>
  static void WriteInterleaved(void *ctx, uint32_t addr, T value);
  static uint32_t ReadRegister(void *ctx, uint32_t addr);
  static void WriteRegister(void *ctx, uint32_t addr, uint32_t value);

  void InitMemory();
  void ReconfigureVideoOutput();
  void ReconfigureSPG();
  void LineClockUpdate();

  emu::Scheduler &scheduler_;
  emu::Memory &memory_;
  Holly &holly_;
  TileAccelerator ta_;
  renderer::Backend *rb_;

  emu::TimerHandle line_timer_;
  int current_scanline_;

  std::chrono::high_resolution_clock::time_point last_frame_, last_vblank_;
  float fps_, vbps_;

  uint8_t *vram_;
  uint8_t *pram_;

  Register regs_[PVR_REG_SIZE >> 2];

#define PVR_REG(offset, name, flags, default, type) \
  type &name{reinterpret_cast<type &>(regs_[name##_OFFSET >> 2].value)};
#include "holly/pvr2_regs.inc"
#undef PVR_REG
};
}
}

#endif
