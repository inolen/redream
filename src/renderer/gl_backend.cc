#include "core/assert.h"
#include "emu/profiler.h"
#include "renderer/gl_backend.h"

using namespace re;
using namespace re::renderer;
using namespace re::ui;

#include "renderer/ta.glsl"
#include "renderer/ui.glsl"

static GLenum filter_funcs[] = {
    GL_NEAREST,                // FILTER_NEAREST
    GL_LINEAR,                 // FILTER_BILINEAR
    GL_NEAREST_MIPMAP_LINEAR,  // FILTER_NEAREST + gen_mipmaps
    GL_LINEAR_MIPMAP_LINEAR    // FILTER_BILINEAR + gen_mipmaps
};

static GLenum wrap_modes[] = {
    GL_REPEAT,          // WRAP_REPEAT
    GL_CLAMP_TO_EDGE,   // WRAP_CLAMP_TO_EDGE
    GL_MIRRORED_REPEAT  // WRAP_MIRRORED_REPEAT
};

static GLenum depth_funcs[] = {
    GL_NONE,      // DEPTH_NONE
    GL_NEVER,     // DEPTH_NEVER
    GL_LESS,      // DEPTH_LESS
    GL_EQUAL,     // DEPTH_EQUAL
    GL_LEQUAL,    // DEPTH_LEQUAL
    GL_GREATER,   // DEPTH_GREATER
    GL_NOTEQUAL,  // DEPTH_NEQUAL
    GL_GEQUAL,    // DEPTH_GEQUAL
    GL_ALWAYS     // DEPTH_ALWAYS
};

static GLenum cull_face[] = {
    GL_NONE,   // CULL_NONE
    GL_FRONT,  // CULL_FRONT
    GL_BACK    // CULL_BACK
};

static GLenum blend_funcs[] = {GL_NONE,
                               GL_ZERO,
                               GL_ONE,
                               GL_SRC_COLOR,
                               GL_ONE_MINUS_SRC_COLOR,
                               GL_SRC_ALPHA,
                               GL_ONE_MINUS_SRC_ALPHA,
                               GL_DST_ALPHA,
                               GL_ONE_MINUS_DST_ALPHA,
                               GL_DST_COLOR,
                               GL_ONE_MINUS_DST_COLOR};

static GLenum prim_types[] = {
    GL_TRIANGLES,  // PRIM_TRIANGLES
    GL_LINES,      // PRIM_LINES
};

GLBackend::GLBackend(Window &window)
    : window_(window), ctx_(nullptr), textures_{0} {
  window_.AddListener(this);
}

GLBackend::~GLBackend() {
  DestroyVertexBuffers();
  DestroyShaders();
  DestroyTextures();
  DestroyContext();

  window_.RemoveListener(this);
}

bool GLBackend::Init() {
  if (!InitContext()) {
    return false;
  }

  CreateTextures();
  CreateShaders();
  CreateVertexBuffers();

  return true;
}

TextureHandle GLBackend::RegisterTexture(PixelFormat format, FilterMode filter,
                                         WrapMode wrap_u, WrapMode wrap_v,
                                         bool gen_mipmaps, int width,
                                         int height, const uint8_t *buffer) {
  // FIXME worth speeding up?
  TextureHandle handle;
  for (handle = 1; handle < MAX_TEXTURES; handle++) {
    if (!textures_[handle]) {
      break;
    }
  }
  CHECK_LT(handle, MAX_TEXTURES);

  GLuint internal_fmt;
  GLuint pixel_fmt;
  switch (format) {
    case PXL_RGBA:
      internal_fmt = GL_RGBA;
      pixel_fmt = GL_UNSIGNED_BYTE;
      break;
    case PXL_RGBA5551:
      internal_fmt = GL_RGBA;
      pixel_fmt = GL_UNSIGNED_SHORT_5_5_5_1;
      break;
    case PXL_RGB565:
      internal_fmt = GL_RGB;
      pixel_fmt = GL_UNSIGNED_SHORT_5_6_5;
      break;
    case PXL_RGBA4444:
      internal_fmt = GL_RGBA;
      pixel_fmt = GL_UNSIGNED_SHORT_4_4_4_4;
      break;
    case PXL_RGBA8888:
      internal_fmt = GL_RGBA;
      pixel_fmt = GL_UNSIGNED_INT_8_8_8_8;
      break;
    default:
      LOG_FATAL("Unexpected pixel format %d", format);
      break;
  }

  GLuint &gltex = textures_[handle];
  glGenTextures(1, &gltex);
  glBindTexture(GL_TEXTURE_2D, gltex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  filter_funcs[filter * gen_mipmaps]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_funcs[filter]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_modes[wrap_u]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_modes[wrap_v]);
  glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, width, height, 0, internal_fmt,
               pixel_fmt, buffer);

  if (gen_mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  glBindTexture(GL_TEXTURE_2D, 0);

  return handle;
}

void GLBackend::FreeTexture(TextureHandle handle) {
  GLuint *gltex = &textures_[handle];
  glDeleteTextures(1, gltex);
  *gltex = 0;
}

void GLBackend::BeginFrame() {
  int width = window_.width();
  int height = window_.height();

  SetDepthMask(true);

  glViewport(0, 0, width, height);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLBackend::EndFrame() { SDL_GL_SwapWindow(window_.handle()); }

void GLBackend::Begin2D() {
  Eigen::Matrix4f ortho = Eigen::Matrix4f::Identity();
  ortho(0, 0) = 2.0f / (float)window_.width();
  ortho(1, 1) = -2.0f / (float)window_.height();
  ortho(0, 3) = -1.0;
  ortho(1, 3) = 1.0;
  ortho(2, 2) = 0;

  Eigen::Matrix4f projection = ortho.transpose();

  SetDepthMask(false);
  SetDepthFunc(DEPTH_NONE);
  SetCullFace(CULL_NONE);

  BindVAO(ui_vao_);
  BindProgram(&ui_program_);
  glUniformMatrix4fv(GetUniform(UNIFORM_MODELVIEWPROJECTIONMATRIX), 1, GL_FALSE,
                     projection.data());
  glUniform1i(GetUniform(UNIFORM_DIFFUSEMAP), MAP_DIFFUSE);
}

void GLBackend::End2D() { SetScissorTest(false); }

void GLBackend::BeginSurfaces2D(const Vertex2D *verts, int num_verts,
                                uint16_t *indices, int num_indices) {
  glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex2D) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  if (indices) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui_ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * num_indices,
                 indices, GL_DYNAMIC_DRAW);
    ui_use_ibo_ = true;
  } else {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, -1);
    ui_use_ibo_ = false;
  }
}

void GLBackend::DrawSurface2D(const Surface2D &surf) {
  if (surf.scissor) {
    SetScissorTest(true);
    SetScissorClip(static_cast<int>(surf.scissor_rect[0]),
                   static_cast<int>(surf.scissor_rect[1]),
                   static_cast<int>(surf.scissor_rect[2]),
                   static_cast<int>(surf.scissor_rect[3]));
  } else {
    SetScissorTest(false);
  }

  SetBlendFunc(surf.src_blend, surf.dst_blend);
  BindTexture(MAP_DIFFUSE, surf.texture ? textures_[surf.texture] : white_tex_);

  if (ui_use_ibo_) {
    glDrawElements(
        prim_types[surf.prim_type], surf.num_verts, GL_UNSIGNED_SHORT,
        reinterpret_cast<void *>(sizeof(uint16_t) * surf.first_vert));
  } else {
    glDrawArrays(prim_types[surf.prim_type], surf.first_vert, surf.num_verts);
  }
}

void GLBackend::EndSurfaces2D() {}

void GLBackend::RenderSurfaces(const Eigen::Matrix4f &projection,
                               const Surface *surfs, int num_surfs,
                               const Vertex *verts, int num_verts,
                               const int *sorted_surfs) {
  PROFILER_GPU("GLBackend::RenderSurfaces");

  if (state_.debug_wireframe) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }

  // transpose to column-major for OpenGL
  Eigen::Matrix4f transposed = projection.transpose();

  glBindBuffer(GL_ARRAY_BUFFER, ta_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  BindVAO(ta_vao_);
  BindProgram(&ta_program_);
  glUniformMatrix4fv(GetUniform(UNIFORM_MODELVIEWPROJECTIONMATRIX), 1, GL_FALSE,
                     transposed.data());
  glUniform1i(GetUniform(UNIFORM_DIFFUSEMAP), MAP_DIFFUSE);

  for (int i = 0; i < num_surfs; i++) {
    const Surface *surf = &surfs[sorted_surfs[i]];

    SetDepthMask(surf->depth_write);
    SetDepthFunc(surf->depth_func);
    SetCullFace(surf->cull);
    SetBlendFunc(surf->src_blend, surf->dst_blend);

    // TODO use surf->shade to select correct shader

    BindTexture(MAP_DIFFUSE,
                surf->texture ? textures_[surf->texture] : white_tex_);
    glDrawArrays(GL_TRIANGLE_STRIP, surf->first_vert, surf->num_verts);
  }

  if (state_.debug_wireframe) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
}

void GLBackend::OnPaint(bool show_main_menu) {
  if (show_main_menu && ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("Render")) {
      ImGui::MenuItem("Wireframe", "", &state_.debug_wireframe);
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }
}

bool GLBackend::InitContext() {
  // need at least a 3.3 core context for our shaders
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  // request a 24-bit depth buffer. 16-bits isn't enough precision when
  // unprojecting dreamcast coordinates, see TileRenderer::GetProjectionMatrix
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  ctx_ = SDL_GL_CreateContext(window_.handle());
  if (!ctx_) {
    LOG_WARNING("OpenGL context creation failed: %s", SDL_GetError());
    return false;
  }

  // link in gl functions at runtime
  glewExperimental = GL_TRUE;
  GLenum err = glewInit();
  if (err != GLEW_OK) {
    LOG_WARNING("GLEW initialization failed: %s", glewGetErrorString(err));
    return false;
  }

  // enable vsync
  SDL_GL_SetSwapInterval(1);

  return true;
}

void GLBackend::DestroyContext() {
  if (!ctx_) {
    return;
  }

  SDL_GL_DeleteContext(ctx_);
  ctx_ = nullptr;
}

void GLBackend::CreateTextures() {
  uint8_t pixels[64 * 64 * 4];
  memset(pixels, 0xff, sizeof(pixels));
  glGenTextures(1, &white_tex_);
  glBindTexture(GL_TEXTURE_2D, white_tex_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
}

void GLBackend::DestroyTextures() {
  if (!ctx_) {
    return;
  }

  glDeleteTextures(1, &white_tex_);

  for (int i = 1; i < MAX_TEXTURES; i++) {
    if (!textures_[i]) {
      continue;
    }
    glDeleteTextures(1, &textures_[i]);
  }
}

void GLBackend::CreateShaders() {
  if (!CompileProgram(&ta_program_, nullptr, ta_vp, ta_fp)) {
    LOG_FATAL("Failed to compile ta shader.");
  }

  if (!CompileProgram(&ui_program_, nullptr, ui_vp, ui_fp)) {
    LOG_FATAL("Failed to compile ui shader.");
  }
}

void GLBackend::DestroyShaders() {
  if (!ctx_) {
    return;
  }

  DestroyProgram(&ta_program_);
  DestroyProgram(&ui_program_);
}

void GLBackend::CreateVertexBuffers() {
  //
  // UI vao
  //
  glGenVertexArrays(1, &ui_vao_);
  glBindVertexArray(ui_vao_);

  glGenBuffers(1, &ui_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);

  glGenBuffers(1, &ui_ibo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui_ibo_);

  // xy
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
                        (void *)offsetof(Vertex2D, xy));

  // texcoord
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
                        (void *)offsetof(Vertex2D, uv));

  // color
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex2D),
                        (void *)offsetof(Vertex2D, color));

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  //
  // TA vao
  //
  glGenVertexArrays(1, &ta_vao_);
  glBindVertexArray(ta_vao_);

  glGenBuffers(1, &ta_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, ta_vbo_);

  // xyz
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, xyz));

  // color
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                        (void *)offsetof(Vertex, color));

  // offset color
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                        (void *)offsetof(Vertex, offset_color));

  // texcoord
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, uv));

  glBindVertexArray(0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GLBackend::DestroyVertexBuffers() {
  if (!ctx_) {
    return;
  }

  glDeleteBuffers(1, &ui_ibo_);
  glDeleteBuffers(1, &ui_vbo_);
  glDeleteVertexArrays(1, &ui_vao_);

  glDeleteBuffers(1, &ta_vbo_);
  glDeleteVertexArrays(1, &ta_vao_);
}

void GLBackend::SetScissorTest(bool enabled) {
  if (state_.scissor_test == enabled) {
    return;
  }

  state_.scissor_test = enabled;

  if (enabled) {
    glEnable(GL_SCISSOR_TEST);
  } else {
    glDisable(GL_SCISSOR_TEST);
  }
}

void GLBackend::SetScissorClip(int x, int y, int width, int height) {
  glScissor(x, y, width, height);
}

void GLBackend::SetDepthMask(bool enabled) {
  if (state_.depth_mask == enabled) {
    return;
  }

  state_.depth_mask = enabled;

  glDepthMask(enabled ? 1 : 0);
}

void GLBackend::SetDepthFunc(DepthFunc fn) {
  if (state_.depth_func == fn) {
    return;
  }

  state_.depth_func = fn;

  if (fn == DEPTH_NONE) {
    glDisable(GL_DEPTH_TEST);
  } else {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(depth_funcs[fn]);
  }
}

void GLBackend::SetCullFace(CullFace fn) {
  if (state_.cull_face == fn) {
    return;
  }

  state_.cull_face = fn;

  if (fn == CULL_NONE) {
    glDisable(GL_CULL_FACE);
  } else {
    glEnable(GL_CULL_FACE);
    glCullFace(cull_face[fn]);
  }
}

void GLBackend::SetBlendFunc(BlendFunc src_fn, BlendFunc dst_fn) {
  if (state_.src_blend == src_fn && state_.dst_blend == dst_fn) {
    return;
  }

  state_.src_blend = src_fn;
  state_.dst_blend = dst_fn;

  if (src_fn == BLEND_NONE || dst_fn == BLEND_NONE) {
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendFunc(blend_funcs[src_fn], blend_funcs[dst_fn]);
  }
}

void GLBackend::BindVAO(GLuint vao) {
  if (state_.current_vao == vao) {
    return;
  }

  state_.current_vao = vao;

  glBindVertexArray(vao);
}

void GLBackend::BindProgram(ShaderProgram *program) {
  if (state_.current_program == program) {
    return;
  }

  state_.current_program = program;

  glUseProgram(program ? program->program : 0);
}

void GLBackend::BindTexture(TextureMap map, GLuint tex) {
  glActiveTexture(GL_TEXTURE0 + map);
  glBindTexture(GL_TEXTURE_2D, tex);
}

GLint GLBackend::GetUniform(UniformAttr attr) {
  return state_.current_program->uniforms[attr];
}
