#ifndef RENDERER_BACKEND_H
#define RENDERER_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

struct window_s;

typedef int texture_handle_t;

typedef enum {
  PXL_INVALID,
  PXL_RGBA,
  PXL_RGBA5551,
  PXL_RGB565,
  PXL_RGBA4444,
  PXL_RGBA8888,
} pxl_format_t;

typedef enum {
  FILTER_NEAREST,
  FILTER_BILINEAR,
  NUM_FILTER_MODES,
} filter_mode_t;

typedef enum {
  WRAP_REPEAT,
  WRAP_CLAMP_TO_EDGE,
  WRAP_MIRRORED_REPEAT,
} wrap_mode_t;

typedef enum {
  DEPTH_NONE,
  DEPTH_NEVER,
  DEPTH_LESS,
  DEPTH_EQUAL,
  DEPTH_LEQUAL,
  DEPTH_GREATER,
  DEPTH_NEQUAL,
  DEPTH_GEQUAL,
  DEPTH_ALWAYS,
} depth_func_t;

typedef enum {
  CULL_NONE,
  CULL_FRONT,
  CULL_BACK,
} cull_face_t;

typedef enum {
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
} blend_func_t;

typedef enum {
  SHADE_DECAL,
  SHADE_MODULATE,
  SHADE_DECAL_ALPHA,
  SHADE_MODULATE_ALPHA,
} shade_mode_t;

typedef enum {
  BOX_BAR,
  BOX_FLAT,
} box_type_t;

typedef enum {
  PRIM_TRIANGLES,
  PRIM_LINES,
} prim_type_t;

typedef struct {
  float xyz[3];
  float uv[2];
  uint32_t color;
  uint32_t offset_color;
} vertex_t;

typedef struct {
  texture_handle_t texture;
  bool depth_write;
  depth_func_t depth_func;
  cull_face_t cull;
  blend_func_t src_blend;
  blend_func_t dst_blend;
  shade_mode_t shade;
  bool ignore_tex_alpha;
  int first_vert;
  int num_verts;
} surface_t;

typedef struct {
  float xy[2];
  float uv[2];
  uint32_t color;
} vertex2d_t;

typedef struct {
  prim_type_t prim_type;
  texture_handle_t texture;
  blend_func_t src_blend;
  blend_func_t dst_blend;
  bool scissor;
  float scissor_rect[4];
  int first_vert;
  int num_verts;
} surface2d_t;

struct rb_s;

void rb_begin_surfaces(struct rb_s *rb, const float *projection,
                       const vertex_t *verts, int num_verts);
void rb_draw_surface(struct rb_s *rb, const surface_t *surf);
void rb_end_surfaces(struct rb_s *rb);

void rb_begin_surfaces2d(struct rb_s *rb, const vertex2d_t *verts,
                         int num_verts, uint16_t *indices, int num_indices);
void rb_draw_surface2d(struct rb_s *rb, const surface2d_t *surf);
void rb_end_surfaces2d(struct rb_s *rb);

void rb_begin2d(struct rb_s *rb);
void rb_end2d(struct rb_s *rb);

void rb_begin_frame(struct rb_s *rb);
void rb_end_frame(struct rb_s *rb);

texture_handle_t rb_register_texture(struct rb_s *rb, pxl_format_t format,
                                     filter_mode_t filter, wrap_mode_t wrap_u,
                                     wrap_mode_t wrap_v, bool mipmaps,
                                     int width, int height,
                                     const uint8_t *buffer);
void rb_free_texture(struct rb_s *rb, texture_handle_t handle);

struct rb_s *rb_create(struct window_s *window);
void rb_destroy(struct rb_s *rb);

#endif
