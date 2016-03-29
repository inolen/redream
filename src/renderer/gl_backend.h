#ifndef GL_BACKEND_H
#define GL_BACKEND_H

#include <GL/glew.h>
#include <SDL_opengl.h>
#include "renderer/backend.h"
#include "renderer/gl_shader.h"
#include "ui/window.h"

namespace re {
namespace renderer {

enum {
  MAX_TEXTURES = 1024,
};

enum TextureMap {
  MAP_DIFFUSE,
};

struct BackendState {
  BackendState()
      : debug_wireframe(false),
        scissor_test(false),
        depth_mask(true),
        depth_func(DEPTH_NONE),
        cull_face(CULL_BACK),
        src_blend(BLEND_NONE),
        dst_blend(BLEND_NONE),
        current_vao(0),
        current_program(nullptr) {}

  bool debug_wireframe;
  bool scissor_test;
  bool depth_mask;
  DepthFunc depth_func;
  CullFace cull_face;
  BlendFunc src_blend;
  BlendFunc dst_blend;
  GLuint current_vao;
  int vertex_attribs;
  ShaderProgram *current_program;
};

class GLBackend : public Backend, public ui::WindowListener {
 public:
  GLBackend(ui::Window &window);
  ~GLBackend();

  bool Init() final;

  TextureHandle RegisterTexture(PixelFormat format, FilterMode filter,
                                WrapMode wrap_u, WrapMode wrap_v, bool mipmaps,
                                int width, int height,
                                const uint8_t *buffer) final;
  void FreeTexture(TextureHandle handle) final;

  void BeginFrame() final;
  void EndFrame() final;

  void Begin2D() final;
  void End2D() final;

  void BeginSurfaces2D(const Vertex2D *verts, int num_verts, uint16_t *indices,
                       int num_indices) final;
  void DrawSurface2D(const Surface2D &surf) final;
  void EndSurfaces2D() final;

  void BeginSurfaces(const Eigen::Matrix4f &projection, const Vertex *verts,
                     int num_verts) final;
  void DrawSurface(const Surface &surf) final;
  void EndSurfaces() final;

 private:
  void OnPaint(bool show_main_menu) final;

  bool InitContext();
  void DestroyContext();

  void CreateTextures();
  void DestroyTextures();
  void CreateShaders();
  void DestroyShaders();
  void CreateVertexBuffers();
  void DestroyVertexBuffers();

  void SetScissorTest(bool enabled);
  void SetScissorClip(int x, int y, int width, int height);
  void SetDepthMask(bool enabled);
  void SetDepthFunc(DepthFunc fn);
  void SetCullFace(CullFace fn);
  void SetBlendFunc(BlendFunc src_fn, BlendFunc dst_fn);
  void BindVAO(GLuint vao);
  void BindProgram(ShaderProgram *program);
  void BindTexture(TextureMap map, GLuint tex);
  GLint GetUniform(UniformAttr attr);

  ui::Window &window_;
  SDL_GLContext ctx_;
  BackendState state_;
  GLuint textures_[MAX_TEXTURES];
  GLuint white_tex_;

  ShaderProgram ta_program_;
  ShaderProgram ui_program_;

  GLuint ta_vao_, ta_vbo_;
  GLuint ui_vao_, ui_vbo_, ui_ibo_;
  bool ui_use_ibo_;
};
}
}

#endif
