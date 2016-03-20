#ifndef TILE_ACCELERATOR_TYPES_H
#define TILE_ACCELERATOR_TYPES_H

namespace re {
namespace hw {
namespace holly {

enum {
  // control params
  TA_PARAM_END_OF_LIST,
  TA_PARAM_USER_TILE_CLIP,
  TA_PARAM_OBJ_LIST_SET,
  TA_PARAM_RESERVED0,
  // global params
  TA_PARAM_POLY_OR_VOL,
  TA_PARAM_SPRITE,
  TA_PARAM_RESERVED1,
  // vertex params
  TA_PARAM_VERTEX,
  TA_NUM_PARAMS,
};

enum {
  TA_NUM_VERT_TYPES = 18,
};

enum {
  TA_LIST_OPAQUE,
  TA_LIST_OPAQUE_MODVOL,
  TA_LIST_TRANSLUCENT,
  TA_LIST_TRANSLUCENT_MODVOL,
  TA_LIST_PUNCH_THROUGH,
  TA_NUM_LISTS,
};

enum {
  TA_PIXEL_1555,
  TA_PIXEL_565,
  TA_PIXEL_4444,
  TA_PIXEL_YUV422,
  TA_PIXEL_BUMPMAP,
  TA_PIXEL_4BPP,
  TA_PIXEL_8BPP,
  TA_PIXEL_RESERVED,
};

enum {
  TA_PAL_ARGB1555,
  TA_PAL_RGB565,
  TA_PAL_ARGB4444,
  TA_PAL_ARGB8888,
};

union PCW {
  struct {
    // obj control
    uint32_t uv_16bit : 1;
    uint32_t gouraud : 1;
    uint32_t offset : 1;
    uint32_t texture : 1;
    uint32_t col_type : 2;
    uint32_t volume : 1;
    uint32_t shadow : 1;
    uint32_t reserved0 : 8;
    // group control
    uint32_t user_clip : 2;
    uint32_t strip_len : 2;
    uint32_t reserved1 : 3;
    uint32_t group_en : 1;
    // para control
    uint32_t list_type : 3;
    uint32_t reserved2 : 1;
    uint32_t end_of_strip : 1;
    uint32_t para_type : 3;
  };
  uint8_t obj_control;
  uint32_t full;
};

// Image Synthesis Processor parameters
union ISP_TSP {
  struct {
    uint32_t reserved : 20;
    uint32_t dcalc_ctrl : 1;
    uint32_t cache_bypass : 1;
    uint32_t uv_16bit : 1;
    uint32_t gouraud_shading : 1;
    uint32_t offset : 1;
    uint32_t texture : 1;
    uint32_t z_write_disable : 1;
    uint32_t culling_mode : 2;
    uint32_t depth_compare_mode : 3;
  };
  uint32_t full;
};

// Texture and Shading Processor parameters
union TSP {
  struct {
    uint32_t texture_v_size : 3;
    uint32_t texture_u_size : 3;
    uint32_t texture_shading_instr : 2;
    uint32_t mipmap_d_adjust : 4;
    uint32_t super_sample_texture : 1;
    uint32_t filter_mode : 2;
    uint32_t clamp_v : 1;
    uint32_t clamp_u : 1;
    uint32_t flip_v : 1;
    uint32_t flip_u : 1;
    uint32_t ignore_tex_alpha : 1;
    uint32_t use_alpha : 1;
    uint32_t color_clamp : 1;
    uint32_t fog_control : 2;
    uint32_t dst_select : 1;
    uint32_t src_select : 1;
    uint32_t dst_alpha_instr : 3;
    uint32_t src_alpha_instr : 3;
  };
  uint32_t full;
};

// Texture parameters
union TCW {
  // rgb, yuv and bumpmap textures
  struct {
    uint32_t texture_addr : 21;
    uint32_t reserved : 4;
    uint32_t stride_select : 1;
    uint32_t scan_order : 1;
    uint32_t pixel_format : 3;
    uint32_t vq_compressed : 1;
    uint32_t mip_mapped : 1;
  };
  // palette textures
  struct {
    uint32_t texture_addr : 21;
    uint32_t palette_selector : 6;
    uint32_t pixel_format : 3;
    uint32_t vq_compressed : 1;
    uint32_t mip_mapped : 1;
  } p;
  uint32_t full;
};

//
// Global parameters
//
union PolyParam {
  struct {
    PCW pcw;
    ISP_TSP isp_tsp;
    TSP tsp;
    TCW tcw;
    uint32_t ignore_0;
    uint32_t ignore_1;
    uint32_t sdma_data_size;
    uint32_t sdma_next_addr;
  } type0;

  struct {
    PCW pcw;
    ISP_TSP isp_tsp;
    TSP tsp;
    TCW tcw;
    float face_color_a;
    float face_color_r;
    float face_color_g;
    float face_color_b;
  } type1;

  struct {
    PCW pcw;
    ISP_TSP isp_tsp;
    TSP tsp;
    TCW tcw;
    uint32_t ignore_0;
    uint32_t ignore_1;
    uint32_t sdma_data_size;
    uint32_t sdma_next_addr;
    float face_color_a;
    float face_color_r;
    float face_color_g;
    float face_color_b;
    float face_offset_color_a;
    float face_offset_color_r;
    float face_offset_color_g;
    float face_offset_color_b;
  } type2;

  struct {
    PCW pcw;
    ISP_TSP isp_tsp;
    TSP tsp0;
    TCW tcw0;
    TSP tsp1;
    TCW tcw1;
    uint32_t sdma_data_size;
    uint32_t sdma_next_addr;
  } type3;

  struct {
    PCW pcw;
    ISP_TSP isp_tsp;
    TSP tsp0;
    TCW tcw0;
    TSP tsp1;
    TCW tcw1;
    uint32_t sdma_data_size;
    uint32_t sdma_next_addr;
    float face_color_a_0;
    float face_color_r_0;
    float face_color_g_0;
    float face_color_b_0;
    float face_color_a_1;
    float face_color_r_1;
    float face_color_g_1;
    float face_color_b_1;
  } type4;

  struct {
    PCW pcw;
    ISP_TSP isp_tsp;
    TSP tsp;
    TCW tcw;
    uint32_t base_color;
    uint32_t offset_color;
    uint32_t sdma_data_size;
    uint32_t sdma_next_addr;
  } sprite;

  struct {
    PCW pcw;
    ISP_TSP isp_tsp;
    uint32_t reserved[6];
  } modvol;
};

//
// Vertex parameters
//
union VertexParam {
  struct {
    PCW pcw;
    float xyz[3];
    uint32_t ignore_0;
    uint32_t ignore_1;
    uint32_t base_color;
    uint32_t ignore_2;
  } type0;

  struct {
    PCW pcw;
    float xyz[3];
    float base_color_a;
    float base_color_r;
    float base_color_g;
    float base_color_b;
  } type1;

  struct {
    PCW pcw;
    float xyz[3];
    uint32_t ignore_0;
    uint32_t ignore_1;
    float base_intensity;
    uint32_t ignore_2;
  } type2;

  struct {
    PCW pcw;
    float xyz[3];
    float uv[2];
    uint32_t base_color;
    uint32_t offset_color;
  } type3;

  struct {
    PCW pcw;
    float xyz[3];
    uint16_t uv[2];
    uint32_t ignore_0;
    uint32_t base_color;
    uint32_t offset_color;
  } type4;

  struct {
    PCW pcw;
    float xyz[3];
    float uv[2];
    uint32_t ignore_0;
    uint32_t ignore_1;
    float base_color_a;
    float base_color_r;
    float base_color_g;
    float base_color_b;
    float offset_color_a;
    float offset_color_r;
    float offset_color_g;
    float offset_color_b;
  } type5;

  struct {
    PCW pcw;
    float xyz[3];
    uint16_t uv[2];
    uint32_t ignore_0;
    uint32_t ignore_1;
    uint32_t ignore_2;
    float base_color_a;
    float base_color_r;
    float base_color_g;
    float base_color_b;
    float offset_color_a;
    float offset_color_r;
    float offset_color_g;
    float offset_color_b;
  } type6;

  struct {
    PCW pcw;
    float xyz[3];
    float uv[2];
    float base_intensity;
    float offset_intensity;
  } type7;

  struct {
    PCW pcw;
    float xyz[3];
    uint16_t uv[2];
    uint32_t ignore_0;
    float base_intensity;
    float offset_intensity;
  } type8;

  struct {
    PCW pcw;
    float xyz[3];
    uint32_t base_color_0;
    uint32_t base_color_1;
    uint32_t ignore_0;
    uint32_t ignore_1;
  } type9;

  struct {
    PCW pcw;
    float xyz[3];
    float base_intensity_0;
    float base_intensity_1;
    uint32_t ignore_0;
    uint32_t ignore_1;
  } type10;

  struct {
    PCW pcw;
    float xyz[3];
    float uv_0[2];
    uint32_t base_color_0;
    uint32_t offset_color_0;
    float uv_1[2];
    uint32_t base_color_1;
    uint32_t offset_color_1;
    uint32_t ignore_0;
    uint32_t ignore_1;
    uint32_t ignore_2;
    uint32_t ignore_3;
  } type11;

  struct {
    PCW pcw;
    float xyz[3];
    uint16_t vu_0[2];
    uint32_t ignore_0;
    uint32_t base_color_0;
    uint32_t offset_color_0;
    uint16_t vu_1[2];
    uint32_t ignore_1;
    uint32_t base_color_1;
    uint32_t offset_color_1;
    uint32_t ignore_2;
    uint32_t ignore_3;
    uint32_t ignore_4;
    uint32_t ignore_5;
  } type12;

  struct {
    PCW pcw;
    float xyz[3];
    float uv_0[2];
    float base_intensity_0;
    float offset_intensity_0;
    float uv_1[2];
    float base_intensity_1;
    float offset_intensity_1;
    uint32_t ignore_0;
    uint32_t ignore_1;
    uint32_t ignore_2;
    uint32_t ignore_3;
  } type13;

  struct {
    PCW pcw;
    float xyz[3];
    uint16_t vu_0[2];
    uint32_t ignore_0;
    float base_intensity_0;
    float offset_intensity_0;
    uint16_t vu_1[2];
    uint32_t ignore_1;
    float base_intensity_1;
    float offset_intensity_1;
    uint32_t ignore_2;
    uint32_t ignore_3;
    uint32_t ignore_4;
    uint32_t ignore_5;
  } type14;

  struct {
    PCW pcw;
    float xyz[4][3];
    uint32_t ignore_0;
    uint32_t ignore_1;
    uint32_t ignore_2;
  } sprite0;

  struct {
    PCW pcw;
    float xyz[4][3];
    uint32_t uv[3];
  } sprite1;
};

// worst case background vertex size, see ISP_BACKGND_T field
static const int BG_VERTEX_SIZE = (0b111 * 2 + 3) * 4 * 3;

struct TileContext {
  uint32_t addr;

  // pvr state
  bool autosort;
  int stride;
  int pal_pxl_format;
  int video_width;
  int video_height;
  ISP_TSP bg_isp;
  TSP bg_tsp;
  TCW bg_tcw;
  float bg_depth;
  uint8_t bg_vertices[BG_VERTEX_SIZE];

  // command buffer
  uint8_t data[0x100000];
  int cursor;
  int size;

  // current global state
  const PolyParam *last_poly;
  const VertexParam *last_vertex;
  int list_type;
  int vertex_type;
};
}
}
}

#endif
