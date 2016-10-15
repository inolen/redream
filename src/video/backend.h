#ifndef VIDEO_BACKEND_H
#define VIDEO_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

struct window;

typedef int texture_handle_t;

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
  bool depth_write;
  enum depth_func depth_func;
  enum cull_face cull;
  enum blend_func src_blend;
  enum blend_func dst_blend;
  enum shade_mode shade;
  bool ignore_tex_alpha;
  int first_vert;
  int num_verts;
};

struct vertex2d {
  float xy[2];
  float uv[2];
  uint32_t color;
};

struct surface2d {
  enum prim_type prim_type;
  texture_handle_t texture;
  enum blend_func src_blend;
  enum blend_func dst_blend;
  bool scissor;
  float scissor_rect[4];
  int first_vert;
  int num_verts;
};

struct video_backend;

void video_begin_surfaces(struct video_backend *video, const float *projection,
                          const struct vertex *verts, int num_verts);
void video_draw_surface(struct video_backend *video,
                        const struct surface *surf);
void video_end_surfaces(struct video_backend *video);

void video_begin_surfaces2d(struct video_backend *video,
                            const struct vertex2d *verts, int num_verts,
                            uint16_t *indices, int num_indices);
void video_draw_surface2d(struct video_backend *video,
                          const struct surface2d *surf);
void video_end_surfaces2d(struct video_backend *video);

void video_begin_ortho(struct video_backend *video);
void video_end_ortho(struct video_backend *video);

void video_begin_frame(struct video_backend *video);
void video_end_frame(struct video_backend *video);

texture_handle_t video_create_texture(
    struct video_backend *video, enum pxl_format format,
    enum filter_mode filter, enum wrap_mode wrap_u, enum wrap_mode wrap_v,
    bool mipmaps, int width, int height, const uint8_t *buffer);
void video_destroy_texture(struct video_backend *video,
                           texture_handle_t handle);

struct video_backend *video_create(struct window *window);
void video_destroy(struct video_backend *video);

#endif
