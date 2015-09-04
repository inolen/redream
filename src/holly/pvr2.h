#ifndef PVR_CLX2_H
#define PVR_CLX2_H

#include "emu/scheduler.h"

namespace dreavm {
namespace emu {
class Dreamcast;
class Scheduler;
}

namespace holly {

class Holly;
class TileAccelerator;

union PARAM_BASE_T {
  uint32_t full;
  struct {
    uint32_t base_address : 24;
    uint32_t reserved : 8;
  };
};

union FB_R_CTRL_T {
  uint32_t full;
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
};

union FB_W_CTRL_T {
  uint32_t full;
  struct {
    uint32_t fb_packmode : 3;
    uint32_t fb_dither : 1;
    uint32_t reserved0 : 4;
    uint32_t fb_kval : 8;
    uint32_t fb_alpha_threshhold : 8;
    uint32_t reserved1 : 8;
  };
};

union FPU_SHAD_SCALE_T {
  uint32_t full;
  struct {
    uint32_t scale_factor : 8;
    uint32_t intensity_volume_mode : 1;
    uint32_t reserved : 23;
  };
};

union FPU_PARAM_CFG_T {
  uint32_t full;
  struct {
    uint32_t first_ptr_burst_size : 4;
    uint32_t ptr_burst_size : 4;
    uint32_t isp_burst_threshold : 6;
    uint32_t tsp_burst_threshold : 6;
    uint32_t reserved : 1;
    uint32_t region_header_type : 1;
    uint32_t reserved1 : 10;
  };
};

union ISP_BACKGND_T_T {
  uint32_t full;
  struct {
    uint32_t tag_offset : 3;
    uint32_t tag_address : 21;
    uint32_t skip : 3;
    uint32_t shadow : 1;
    uint32_t cache_bypass : 1;
  };
};

union ISP_FEED_CFG_T {
  uint32_t full;
  struct {
    uint32_t presort : 1;
    uint32_t reserved : 2;
    uint32_t discard : 1;
    uint32_t punch_size : 10;
    uint32_t cache_size : 10;
    uint32_t reserved1 : 8;
  };
};

union SPG_HBLANK_INT_T {
  uint32_t full;
  struct {
    uint32_t line_comp_val : 10;
    uint32_t reserved : 2;
    uint32_t hblank_int_mode : 2;
    uint32_t reserved2 : 2;
    uint32_t hblank_in_interrupt : 10;
    uint32_t reserved3 : 6;
  };
};

union SPG_VBLANK_INT_T {
  uint32_t full;
  struct {
    uint32_t vblank_in_line_number : 10;
    uint32_t reserved : 6;
    uint32_t vblank_out_line_number : 10;
    uint32_t reserved2 : 6;
  };
};

union SPG_CONTROL_T {
  uint32_t full;
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
};

union SPG_LOAD_T {
  uint32_t full;
  struct {
    uint32_t hcount : 10;
    uint32_t reserved : 6;
    uint32_t vcount : 10;
    uint32_t reserved2 : 6;
  };
};

union SPG_VBLANK_T {
  uint32_t full;
  struct {
    uint32_t vbstart : 10;
    uint32_t reserved : 6;
    uint32_t vbend : 10;
    uint32_t reserved2 : 6;
  };
};

union TEXT_CONTROL_T {
  uint32_t full;
  struct {
    uint32_t stride : 5;
    uint32_t reserved : 3;
    uint32_t bankbit : 5;
    uint32_t reserved2 : 3;
    uint32_t index_endian : 1;
    uint32_t codebook_endian : 1;
    uint32_t reserved3 : 14;
  };
};

union PAL_RAM_CTRL_T {
  uint32_t full;
  struct {
    uint32_t pixel_format : 2;
    uint32_t reserved0 : 30;
  };
};

union SPG_STATUS_T {
  uint32_t full;
  struct {
    uint32_t scanline : 10;
    uint32_t fieldnum : 1;
    uint32_t blank : 1;
    uint32_t hsync : 1;
    uint32_t vsync : 1;
    uint32_t reserved : 18;
  };
};

union TA_ISP_BASE_T {
  uint32_t full;
  struct {
    uint32_t base_address : 24;
    uint32_t reserved : 8;
  };
};

class PVR2 {
 public:
  PVR2(emu::Dreamcast *dc);

  float fps() { return fps_; }
  float vbps() { return vbps_; }

  void Init();

  uint32_t ReadRegister32(uint32_t addr);
  void WriteRegister32(uint32_t addr, uint32_t value);

  uint8_t ReadInterleaved8(uint32_t addr);
  uint16_t ReadInterleaved16(uint32_t addr);
  uint32_t ReadInterleaved32(uint32_t addr);
  void WriteInterleaved16(uint32_t addr, uint16_t value);
  void WriteInterleaved32(uint32_t addr, uint32_t value);

 private:
  void ReconfigureSPG();
  void LineClockUpdate();

  emu::Dreamcast *dc_;
  emu::Scheduler *scheduler_;
  holly::Holly *holly_;
  holly::TileAccelerator *ta_;
  emu::Register *pvr_regs_;
  uint8_t *video_ram_;

  emu::TimerHandle line_timer_;
  uint32_t current_scanline_;
  std::chrono::high_resolution_clock::time_point last_frame_;
  float fps_;
  std::chrono::high_resolution_clock::time_point last_vblank_;
  float vbps_;
};
}
}

#endif
