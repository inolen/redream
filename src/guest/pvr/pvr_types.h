#ifndef PVR_TYPES_H
#define PVR_TYPES_H

#include <stdint.h>

/*
 * pvr registers
 */

union param_base {
  uint32_t full;
  struct {
    uint32_t base_address : 24;
    uint32_t : 8;
  };
};

union fb_r_ctrl {
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

union fb_w_ctrl {
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

union fb_r_size {
  uint32_t full;
  struct {
    uint32_t x : 10;
    uint32_t y : 10;
    uint32_t mod : 10;
    uint32_t : 2;
  };
};

union fpu_shad_scale {
  uint32_t full;
  struct {
    uint32_t scale_factor : 8;
    uint32_t intensity_volume_mode : 1;
    uint32_t : 23;
  };
};

union fpu_param_cfg {
  uint32_t full;
  struct {
    uint32_t first_ptr_burst_size : 4;
    uint32_t ptr_burst_size : 4;
    uint32_t isp_burst_threshold : 6;
    uint32_t tsp_burst_threshold : 6;
    uint32_t : 1;
    uint32_t region_header_type : 1;
    uint32_t reserved1 : 10;
  };
};

union isp_backgnd_t {
  uint32_t full;
  struct {
    uint32_t tag_offset : 3;
    uint32_t tag_address : 21;
    uint32_t skip : 3;
    uint32_t shadow : 1;
    uint32_t cache_bypass : 1;
  };
};

union isp_feed_cfg {
  uint32_t full;
  struct {
    uint32_t presort : 1;
    uint32_t : 2;
    uint32_t discard : 1;
    uint32_t punch_size : 10;
    uint32_t cache_size : 10;
    uint32_t reserved1 : 8;
  };
};

union spg_hblank_int {
  uint32_t full;
  struct {
    uint32_t line_comp_val : 10;
    uint32_t : 2;
    uint32_t hblank_int_mode : 2;
    uint32_t : 2;
    uint32_t hblank_in_interrupt : 10;
    uint32_t reserved3 : 6;
  };
};

union spg_vblank_int {
  uint32_t full;
  struct {
    uint32_t vblank_in_line_number : 10;
    uint32_t : 6;
    uint32_t vblank_out_line_number : 10;
    uint32_t : 6;
  };
};

union spg_control {
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
    uint32_t : 22;
  };
};

union spg_load {
  uint32_t full;
  struct {
    uint32_t hcount : 10;
    uint32_t : 6;
    uint32_t vcount : 10;
    uint32_t : 6;
  };
};

union spg_hblank {
  uint32_t full;
  struct {
    uint32_t hbstart : 10;
    uint32_t : 6;
    uint32_t hbend : 10;
    uint32_t : 6;
  };
};

union spg_vblank {
  uint32_t full;
  struct {
    uint32_t vbstart : 10;
    uint32_t : 6;
    uint32_t vbend : 10;
    uint32_t : 6;
  };
};

union text_control {
  uint32_t full;
  struct {
    uint32_t stride : 5;
    uint32_t : 3;
    uint32_t bankbit : 5;
    uint32_t : 3;
    uint32_t index_endian : 1;
    uint32_t codebook_endian : 1;
    uint32_t reserved3 : 14;
  };
};

union vo_control {
  uint32_t full;
  struct {
    uint32_t hsync_pol : 1;
    uint32_t vsync_pol : 1;
    uint32_t blank_pol : 1;
    uint32_t blank_video : 1;
    uint32_t field_mode : 4;
    uint32_t pixel_double : 1;
    uint32_t : 7;
    uint32_t pclk_delay : 6;
    uint32_t : 10;
  };
};

union scaler_ctl {
  uint32_t full;
  struct {
    uint32_t scale_y : 16;
    uint32_t scale_x : 1;
    uint32_t interlace : 1;
    uint32_t field_select : 1;
    uint32_t : 13;
  };
};

union pal_ram_ctrl {
  uint32_t full;
  struct {
    uint32_t pixel_fmt : 2;
    uint32_t reserved0 : 30;
  };
};

union spg_status {
  uint32_t full;
  struct {
    uint32_t scanline : 10;
    uint32_t fieldnum : 1;
    uint32_t blank : 1;
    uint32_t hsync : 1;
    uint32_t vsync : 1;
    uint32_t : 18;
  };
};

union pt_alpha_ref {
  uint32_t full;
  struct {
    uint32_t alpha_ref : 8;
    uint32_t : 24;
  };
};

union ta_isp_base {
  uint32_t full;
  struct {
    uint32_t base_address : 24;
    uint32_t : 8;
  };
};

union ta_yuv_tex_base {
  uint32_t full;
  struct {
    uint32_t base_address : 24;
    uint32_t : 8;
  };
};

union ta_yuv_tex_ctrl {
  uint32_t full;
  struct {
    uint32_t u_size : 6;
    uint32_t : 2;
    uint32_t v_size : 6;
    uint32_t : 2;
    uint32_t tex : 1;
    uint32_t : 7;
    uint32_t format : 1;
    uint32_t : 7;
  };
};

union ta_yuv_tex_cnt {
  uint32_t full;
  struct {
    uint32_t num : 13;
    uint32_t : 19;
  };
};

enum {
#define PVR_REG(addr, name, flags, default_value) \
  name = (addr - 0x005f8000) >> 2,
#include "guest/pvr/pvr_regs.inc"
#undef PVR_REG
  PVR_NUM_REGS = 0x00002000 >> 2,
};

#endif
