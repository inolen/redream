#ifndef RENDERER_BACKEND_H
#define RENDERER_BACKEND_H

#include <Eigen/Dense>

namespace re {
namespace renderer {

typedef int TextureHandle;

enum PixelFormat {
  PXL_INVALID,
  PXL_RGBA,
  PXL_RGBA5551,
  PXL_RGB565,
  PXL_RGBA4444,
  PXL_RGBA8888,
};

enum FilterMode {
  FILTER_NEAREST,
  FILTER_BILINEAR,
};

enum WrapMode {
  WRAP_REPEAT,
  WRAP_CLAMP_TO_EDGE,
  WRAP_MIRRORED_REPEAT,
};

enum DepthFunc {
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

enum CullFace {
  CULL_NONE,
  CULL_FRONT,
  CULL_BACK,
};

enum BlendFunc {
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

enum ShadeMode {
  SHADE_DECAL,
  SHADE_MODULATE,
  SHADE_DECAL_ALPHA,
  SHADE_MODULATE_ALPHA,
};

enum BoxType {
  BOX_BAR,
  BOX_FLAT,
};

enum PrimativeType {
  PRIM_TRIANGLES,
  PRIM_LINES,
};

struct Vertex {
  float xyz[3];
  float uv[2];
  uint32_t color;
  uint32_t offset_color;
};

struct Surface {
  TextureHandle texture;
  bool depth_write;
  DepthFunc depth_func;
  CullFace cull;
  BlendFunc src_blend;
  BlendFunc dst_blend;
  ShadeMode shade;
  bool ignore_tex_alpha;
  int first_vert;
  int num_verts;
};

struct Vertex2D {
  float xy[2];
  float uv[2];
  uint32_t color;
};

struct Surface2D {
  PrimativeType prim_type;
  TextureHandle texture;
  BlendFunc src_blend;
  BlendFunc dst_blend;
  bool scissor;
  float scissor_rect[4];
  int first_vert;
  int num_verts;
};

class Backend {
 public:
  virtual ~Backend() {}

  virtual bool Init() = 0;

  virtual TextureHandle RegisterTexture(PixelFormat format, FilterMode filter,
                                        WrapMode wrap_u, WrapMode wrap_v,
                                        bool mipmaps, int width, int height,
                                        const uint8_t *buffer) = 0;
  virtual void FreeTexture(TextureHandle handle) = 0;

  virtual void BeginFrame() = 0;
  virtual void EndFrame() = 0;

  virtual void Begin2D() = 0;
  virtual void End2D() = 0;

  virtual void BeginSurfaces2D(const Vertex2D *verts, int num_verts,
                               uint16_t *indices, int num_indices) = 0;
  virtual void DrawSurface2D(const Surface2D &surf) = 0;
  virtual void EndSurfaces2D() = 0;

  virtual void BeginSurfaces(const Eigen::Matrix4f &projection,
                             const Vertex *verts, int num_verts) = 0;
  virtual void DrawSurface(const Surface &surf) = 0;
  virtual void EndSurfaces() = 0;
};
}
}

#endif
