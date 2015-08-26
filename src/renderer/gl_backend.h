#ifndef GL_BACKEND_H
#define GL_BACKEND_H

#include <GL/glew.h>
#include <SDL_opengl.h>
#include <stb_truetype.h>
#include <unordered_map>
#include "renderer/backend.h"
#include "renderer/gl_context.h"
#include "renderer/gl_shader.h"

namespace dreavm {
namespace renderer {

enum {  //
  MAX_TEXTURES = 1024,
  MAX_2D_VERTICES = 16384,
  MAX_2D_SURFACES = 256
};

enum TextureMap {  //
  MAP_DIFFUSE
};

struct BakedFont {
  int tw, th;
  float ascent;
  stbtt_packedchar chars[0xff];
  intptr_t texture;
};

struct BackendState {
  BackendState()
      : video_width(0),
        video_height(0),
        ta_width(0),
        ta_height(0),
        depth_mask(true),
        depth_func(DEPTH_NONE),
        cull_face(CULL_BACK),
        src_blend(BLEND_NONE),
        dst_blend(BLEND_NONE),
        current_vao(0),
        current_program(nullptr) {}

  // gl context dimensions
  int video_width;
  int video_height;
  // ta output dimensions
  int ta_width;
  int ta_height;
  bool depth_mask;
  DepthFunc depth_func;
  CullFace cull_face;
  BlendFunc src_blend;
  BlendFunc dst_blend;
  GLuint current_vao;
  int vertex_attribs;
  ShaderProgram *current_program;
};

class GLBackend : public Backend {
  friend class GLProfilerBackend;

 public:
  GLBackend(GLContext &ctx);
  ~GLBackend();

  int video_width() { return state_.video_width; }
  int video_height() { return state_.video_height; }

  bool Init();

  TextureHandle RegisterTexture(PixelFormat format, FilterMode filter,
                                bool gen_mipmaps, int width, int height,
                                const uint8_t *buffer);
  void FreeTexture(TextureHandle handle);

  void SetFramebufferSize(Framebuffer fb, int width, int height);
  void GetFramebufferSize(Framebuffer fb, int *width, int *height);

  void BeginFrame();
  void BindFramebuffer(Framebuffer fb);
  void Clear(float r, float g, float b, float a);
  void RenderFramebuffer(Framebuffer fb);
  void RenderText2D(int x, int y, float point_size, uint32_t color,
                    const char *text);
  void RenderBox2D(int x0, int y0, int x1, int y1, uint32_t color,
                   BoxType type);
  void RenderLine2D(float *verts, int num_verts, uint32_t color);
  void RenderSurfaces(const Eigen::Matrix4f &projection, const Surface *surfs,
                      int num_surfs, const Vertex *verts, int num_verts,
                      const int *sorted_surfs);

  void EndFrame();

 private:
  void InitFramebuffers();
  void DestroyFramebuffers();
  void InitTextures();
  void DestroyTextures();
  void InitShaders();
  void DestroyShaders();
  void InitVertexBuffers();
  void DestroyVertexBuffers();
  void DestroyFonts();

  void SetupDefaultState();
  void SetDepthMask(bool enabled);
  void SetDepthFunc(DepthFunc fn);
  void SetCullFace(CullFace fn);
  void SetBlendFunc(BlendFunc src_fn, BlendFunc dst_fn);
  void BindVAO(GLuint vao);
  void BindProgram(ShaderProgram *program);
  void BindTexture(TextureMap map, GLuint tex);
  GLint GetUniform(UniformAttr attr);
  const BakedFont *GetFont(float point_size);

  Eigen::Matrix4f Ortho2D();
  Vertex2D *AllocVertices2D(const Surface2D &desc, int count);
  void Flush2D();

  GLContext &ctx_;
  BackendState state_;
  GLuint textures_[MAX_TEXTURES];
  GLuint white_tex_;

  ShaderProgram ta_program_;
  ShaderProgram ui_program_;
  GLuint ui_vao_, ui_vbo_;
  GLuint ta_vao_, ta_vbo_, ta_ibo_;

  GLuint fb_ta_, fb_ta_color_, fb_ta_depth_;

  Vertex2D verts2d_[MAX_2D_VERTICES];
  int num_verts2d_;

  Surface2D surfs2d_[MAX_2D_SURFACES];
  int num_surfs2d_;

  std::unordered_map<float, BakedFont *> fonts_;
};
}
}

#endif
