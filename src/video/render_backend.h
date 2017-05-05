#ifndef RENDER_BACKEND_H
#define RENDER_BACKEND_H

#include <stdint.h>

struct window;

typedef unsigned framebuffer_handle_t;
typedef unsigned texture_handle_t;
typedef void *sync_handle_t;

enum pxl_format {
  PXL_INVALID,
  PXL_RGBA,
  PXL_RGBA5551,
  PXL_RGB565,
  PXL_RGBA4444,
  PXL_RGBA8888,
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

enum box_type {
  BOX_BAR,
  BOX_FLAT,
};

enum prim_type {
  PRIM_TRIANGLES,
  PRIM_LINES,
};

struct vertex {
  float xyz[3];
  float uv[2];
  uint32_t color;
  uint32_t offset_color;
};

struct surface {
  texture_handle_t texture;
  int depth_write;
  enum depth_func depth_func;
  enum cull_face cull;
  enum blend_func src_blend;
  enum blend_func dst_blend;

  enum shade_mode shade;
  int ignore_alpha;
  int ignore_texture_alpha;
  int offset_color;
  int pt_alpha_test;
  float pt_alpha_ref;

  int first_vert;
  int num_verts;
};

struct vertex2 {
  float xy[2];
  float uv[2];
  uint32_t color;
};

struct surface2 {
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

struct render_backend *r_create(struct window *window);
struct render_backend *r_create_from(struct render_backend *other);
void r_destroy(struct render_backend *rc);

int r_video_width(struct render_backend *r);
int r_video_height(struct render_backend *r);

framebuffer_handle_t r_create_framebuffer(struct render_backend *r,
                                          texture_handle_t *color_componet);
void r_bind_framebuffer(struct render_backend *r, framebuffer_handle_t handle);
void r_destroy_framebuffer(struct render_backend *r,
                           framebuffer_handle_t handle);

texture_handle_t r_create_texture(struct render_backend *r,
                                  enum pxl_format format,
                                  enum filter_mode filter,
                                  enum wrap_mode wrap_u, enum wrap_mode wrap_v,
                                  int mipmaps, int width, int height,
                                  const uint8_t *buffer);
void r_destroy_texture(struct render_backend *r, texture_handle_t handle);

sync_handle_t r_insert_sync(struct render_backend *r);
void r_wait_sync(struct render_backend *r, sync_handle_t handle);
void r_destroy_sync(struct render_backend *r, sync_handle_t handle);

void r_clear_viewport(struct render_backend *r);
void r_swap_buffers(struct render_backend *r);

void r_begin_ortho(struct render_backend *r);
void r_end_ortho(struct render_backend *r);

void r_begin_surfaces(struct render_backend *r, const float *projection,
                      const struct vertex *verts, int num_verts);
void r_draw_surface(struct render_backend *r, const struct surface *surf);
void r_end_surfaces(struct render_backend *r);

void r_begin_surfaces2(struct render_backend *r, const struct vertex2 *verts,
                       int num_verts, uint16_t *indices, int num_indices);
void r_draw_surface2(struct render_backend *r, const struct surface2 *surf);
void r_end_surfaces2(struct render_backend *r);

#endif
