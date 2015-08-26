#ifndef RENDERER_BACKEND_H
#define RENDERER_BACKEND_H

#include <Eigen/Dense>

namespace dreavm {
namespace renderer {

typedef int TextureHandle;

enum FilterMode {  //
  FILTER_NEAREST,
  FILTER_BILINEAR
};

enum Framebuffer {  //
  FB_DEFAULT,
  FB_TILE_ACCELERATOR
};

enum PixelFormat {
  PXL_INVALID,
  PXL_RGBA5551,
  PXL_RGB565,
  PXL_RGBA4444,
  PXL_RGBA8888
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
  DEPTH_ALWAYS
};

enum CullFace {
  CULL_NONE,  //
  CULL_FRONT,
  CULL_BACK
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
  BLEND_ONE_MINUS_DST_COLOR
};

enum ShadeMode {
  SHADE_DECAL,
  SHADE_MODULATE,
  SHADE_DECAL_ALPHA,
  SHADE_MODULATE_ALPHA
};

enum BoxType { BOX_BAR, BOX_FLAT };

struct Vertex {
  float xyz[3];
  float color[4];
  float offset_color[4];
  float uv[2];
};

struct Surface {
  Surface() : texture(0), num_verts(0) {}

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
  float x, y;
  uint32_t color;
  float u, v;
};

struct Surface2D {
  int prim_type;
  int texture;
  BlendFunc src_blend;
  BlendFunc dst_blend;
  int num_verts;
};

class Backend {
 public:
  virtual ~Backend() {}

  virtual int video_width() = 0;
  virtual int video_height() = 0;

  virtual bool Init() = 0;

  virtual TextureHandle RegisterTexture(PixelFormat format, FilterMode filter,
                                        bool gen_mipmaps, int width, int height,
                                        const uint8_t *buffer) = 0;
  virtual void FreeTexture(TextureHandle handle) = 0;

  virtual void SetFramebufferSize(Framebuffer fb, int width, int height) = 0;
  virtual void GetFramebufferSize(Framebuffer fb, int *width, int *height) = 0;

  virtual void BeginFrame() = 0;
  virtual void BindFramebuffer(Framebuffer fb) = 0;
  virtual void Clear(float r, float g, float b, float a) = 0;
  virtual void RenderFramebuffer(Framebuffer fb) = 0;
  virtual void RenderText2D(int x, int y, float point_size, uint32_t color,
                            const char *text) = 0;
  virtual void RenderBox2D(int x0, int y0, int x1, int y1, uint32_t color,
                           BoxType type) = 0;
  virtual void RenderLine2D(float *verts, int num_verts, uint32_t color) = 0;
  virtual void RenderSurfaces(const Eigen::Matrix4f &projection,
                              const Surface *surfs, int num_surfs,
                              const Vertex *verts, int num_verts,
                              const int *sorted_surfs) = 0;
  virtual void EndFrame() = 0;
};
}
}

#endif
