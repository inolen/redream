#ifndef RENDER_BACKEND_H
#define RENDER_BACKEND_H

#include <stdint.h>

struct host;

/* note, this can't be larger than the width of ta_surface's texture param */
#define MAX_TEXTURES (1 << 13)

typedef int texture_handle_t;

enum pxl_format {
  PXL_RGB,
  PXL_RGBA,
  PXL_RGBA5551,
  PXL_RGB565,
  PXL_RGBA4444,
};

enum filter_mode {
  FILTER_NEAREST,
  FILTER_BILINEAR,
  NUM_FILTER_MODES,
};

enum wrap_mode {
  WRAP_REPEAT,
  WRAP_CLAMP_TO_EDGE,
  WRAP_MIRRORED_REPEAT,
};

enum depth_func {
  DEPTH_NONE,
  DEPTH_NEVER,
  DEPTH_LESS,
  DEPTH_EQUAL,
  DEPTH_LEQUAL,
  DEPTH_GREATER,
  DEPTH_NEQUAL,
  DEPTH_GEQUAL,
  DEPTH_ALWAYS,
};

enum cull_face {
  CULL_NONE,
  CULL_FRONT,
  CULL_BACK,
};

enum blend_func {
  BLEND_NONE,
  BLEND_ZERO,
  BLEND_ONE,
  BLEND_SRC_COLOR,
  BLEND_ONE_MINUS_SRC_COLOR,
  BLEND_SRC_ALPHA,
  BLEND_ONE_MINUS_SRC_ALPHA,
  BLEND_DST_ALPHA,
  BLEND_ONE_MINUS_DST_ALPHA,
  BLEND_DST_COLOR,
  BLEND_ONE_MINUS_DST_COLOR,
};

enum shade_mode {
  SHADE_DECAL,
  SHADE_MODULATE,
  SHADE_DECAL_ALPHA,
  SHADE_MODULATE_ALPHA,
};

enum prim_type {
  PRIM_TRIANGLES,
  PRIM_LINES,
};

struct ta_vertex {
  float xyz[3];
  float uv[2];
  uint32_t color;
  uint32_t offset_color;
};

struct ta_surface {
  union {
    uint64_t full;

    struct {
      uint64_t texture : 13;
      uint64_t depth_write : 1;
      uint64_t depth_func : 4;
      uint64_t cull : 2;
      uint64_t src_blend : 4;
      uint64_t dst_blend : 4;
      uint64_t shade : 3;
      uint64_t ignore_alpha : 1;
      uint64_t ignore_texture_alpha : 1;
      uint64_t offset_color : 1;
      uint64_t alpha_test : 1;
      uint64_t alpha_ref : 8;
      uint64_t debug_depth : 1;
      uint64_t : 19;
    };
  } params;

  int first_vert;
  int num_verts;

  /* first vertex's offset from start of original tristrip, used to control
     winding order when generating indices */
  int strip_offset;
};

struct ui_vertex {
  float xy[2];
  float uv[2];
  uint32_t color;
};

struct ui_surface {
  enum prim_type prim_type;
  texture_handle_t texture;
  enum blend_func src_blend;
  enum blend_func dst_blend;
  int scissor;
  float scissor_rect[4];

  int first_vert;
  int num_verts;
};

struct render_backend;

struct render_backend *r_create(int width, int height);
void r_destroy(struct render_backend *r);

int r_width(struct render_backend *r);
int r_height(struct render_backend *r);

texture_handle_t r_create_texture(struct render_backend *r,
                                  enum pxl_format format,
                                  enum filter_mode filter,
                                  enum wrap_mode wrap_u, enum wrap_mode wrap_v,
                                  int mipmaps, int width, int height,
                                  const uint8_t *buffer);
void r_destroy_texture(struct render_backend *r, texture_handle_t handle);

void r_clear(struct render_backend *r);
void r_viewport(struct render_backend *r, int x, int y, int width, int height);

void r_draw_pixels(struct render_backend *r, const uint8_t *pixels, int x,
                   int y, int width, int height);

void r_begin_ta_surfaces(struct render_backend *r, int video_width,
                         int video_height, const struct ta_vertex *verts,
                         int num_verts, const uint16_t *indices,
                         int num_indices);
void r_draw_ta_surface(struct render_backend *r, const struct ta_surface *surf);
void r_end_ta_surfaces(struct render_backend *r);

void r_begin_ui_surfaces(struct render_backend *r,
                         const struct ui_vertex *verts, int num_verts,
                         const uint16_t *indices, int num_indices);
void r_draw_ui_surface(struct render_backend *r, const struct ui_surface *surf);
void r_end_ui_surfaces(struct render_backend *r);

#endif
