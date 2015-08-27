#include <memory>
#include "core/core.h"
#include "renderer/gl_backend.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

using namespace dreavm::core;
using namespace dreavm::renderer;

#include "inconsolata_ttf.inc"
#include "renderer/ta.glsl"
#include "renderer/ui.glsl"

#define Q0(d, member, v) d[0].member = v
#define Q1(d, member, v) \
  d[1].member = v;       \
  d[3].member = v
#define Q2(d, member, v) d[4].member = v
#define Q3(d, member, v) \
  d[2].member = v;       \
  d[5].member = v

static GLenum filter_funcs[] = {
    GL_NEAREST,                // FILTER_NEAREST
    GL_LINEAR,                 // FILTER_BILINEAR
    GL_NEAREST_MIPMAP_LINEAR,  // FILTER_NEAREST + gen_mipmaps
    GL_LINEAR_MIPMAP_LINEAR    // FILTER_BILINEAR + gen_mipmaps
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

static GLenum blend_funcs[] = {
    GL_NONE, GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR,
    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA,
    GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR};

GLBackend::GLBackend(GLContext &ctx)
    : ctx_(ctx), textures_{0}, fb_ta_(0), num_verts2d_(0), num_surfs2d_(0) {}

GLBackend::~GLBackend() {
  DestroyFonts();
  DestroyVertexBuffers();
  DestroyShaders();
  DestroyTextures();
  DestroyFramebuffers();
}

bool GLBackend::Init() {
  if (!ctx_.GLInit(&state_.video_width, &state_.video_height)) {
    return false;
  }

  // can't initialize framebuffers here, must wait for first resize
  // InitFramebuffers();
  InitTextures();
  InitShaders();
  InitVertexBuffers();

  SetupDefaultState();

  return true;
}

TextureHandle GLBackend::RegisterTexture(PixelFormat format, FilterMode filter,
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

  GLenum min_filter = filter_funcs[filter * gen_mipmaps];
  GLenum mag_filter = filter_funcs[filter];

  GLuint &gltex = textures_[handle];
  glGenTextures(1, &gltex);
  glBindTexture(GL_TEXTURE_2D, gltex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
  glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, width, height, 0, internal_fmt,
               pixel_fmt, buffer);

  if (gen_mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  glBindTexture(GL_TEXTURE_2D, 0);

  return handle;
}

void GLBackend::FreeTexture(TextureHandle handle) {
  GLuint &gltex = textures_[handle];
  glDeleteTextures(1, &gltex);
  gltex = 0;
}

void GLBackend::SetFramebufferSize(Framebuffer fb, int width, int height) {
  switch (fb) {
    case FB_DEFAULT:
      state_.video_width = width;
      state_.video_height = height;
      break;

    case FB_TILE_ACCELERATOR:
      state_.ta_width = width;
      state_.ta_height = height;
      DestroyFramebuffers();
      InitFramebuffers();
      break;
  }
}

void GLBackend::GetFramebufferSize(Framebuffer fb, int *width, int *height) {
  switch (fb) {
    case FB_DEFAULT:
      *width = state_.video_width;
      *height = state_.video_height;
      break;

    case FB_TILE_ACCELERATOR:
      *width = state_.ta_width;
      *height = state_.ta_height;
      break;
  }
}

void GLBackend::BeginFrame() {
  BindFramebuffer(FB_DEFAULT);

  Clear(0.0f, 0.0f, 0.0f, 1.0f);
}

void GLBackend::BindFramebuffer(Framebuffer fb) {
  Flush2D();

  GLuint buffer = 0;
  int width = 0;
  int height = 0;

  switch (fb) {
    case FB_DEFAULT:
      buffer = 0;
      width = state_.video_width;
      height = state_.video_height;
      break;

    case FB_TILE_ACCELERATOR:
      // ensure ta was initialized through SetFramebufferSize
      CHECK_GT(fb_ta_, 0);
      buffer = fb_ta_;
      width = state_.ta_width;
      height = state_.ta_height;
      break;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, buffer);
  glViewport(0, 0, width, height);
  glScissor(0, 0, width, height);
}

void GLBackend::Clear(float r, float g, float b, float a) {
  SetDepthMask(true);

  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLBackend::RenderFramebuffer(Framebuffer fb) {
  switch (fb) {
    case FB_DEFAULT:
      LOG_FATAL("Unsupported");
      break;

    case FB_TILE_ACCELERATOR:
      Vertex2D *vert = AllocVertices2D(
          {GL_TRIANGLES, (int)fb_ta_color_, BLEND_NONE, BLEND_NONE, 0}, 6);

      Q0(vert, x, 0.0f);
      Q0(vert, y, 0.0f);
      Q0(vert, color, 0xffffffff);
      Q0(vert, u, 0.0f);
      Q0(vert, v, 1.0f);

      Q1(vert, x, (float)state_.video_width);
      Q1(vert, y, 0.0f);
      Q1(vert, color, 0xffffffff);
      Q1(vert, u, 1.0f);
      Q1(vert, v, 1.0f);

      Q2(vert, x, (float)state_.video_width);
      Q2(vert, y, (float)state_.video_height);
      Q2(vert, color, 0xffffffff);
      Q2(vert, u, 1.0f);
      Q2(vert, v, 0.0f);

      Q3(vert, x, 0.0f);
      Q3(vert, y, (float)state_.video_height);
      Q3(vert, color, 0xffffffff);
      Q3(vert, u, 0.0f);
      Q3(vert, v, 0.0f);
      break;
  }
}

void GLBackend::RenderText2D(int x, int y, float point_size, uint32_t color,
                             const char *text) {
  float fx = (float)x;
  float fy = (float)y;
  const BakedFont *font = GetFont(point_size);

  int len = strlen(text);
  Vertex2D *vert =
      AllocVertices2D({GL_TRIANGLES, (int)font->texture, BLEND_SRC_ALPHA,
                       BLEND_ONE_MINUS_SRC_ALPHA, 0},
                      6 * len);

  // convert color from argb -> abgr
  color = (color & 0xff000000) | ((color & 0xff) << 16) | (color & 0xff00) |
          ((color & 0xff0000) >> 16);

  // stbtt_GetPackedQuad treats the y parameter as the character's baseline.
  // however, the incoming y represents the top of the text. offset it by the
  // font's ascent (distance from top -> baseline) to compensate
  fy += font->ascent;

  while (*text) {
    if (*text >= 32 /* && *text < 128*/) {
      stbtt_aligned_quad q;
      stbtt_GetPackedQuad(&font->chars[0], font->tw, font->th, *text, &fx, &fy,
                          &q, 0);

      Q0(vert, x, q.x0);
      Q0(vert, y, q.y0);
      Q0(vert, color, color);
      Q0(vert, u, q.s0);
      Q0(vert, v, q.t0);

      Q1(vert, x, q.x1);
      Q1(vert, y, q.y0);
      Q1(vert, color, color);
      Q1(vert, u, q.s1);
      Q1(vert, v, q.t0);

      Q2(vert, x, q.x1);
      Q2(vert, y, q.y1);
      Q2(vert, color, color);
      Q2(vert, u, q.s1);
      Q2(vert, v, q.t1);

      Q3(vert, x, q.x0);
      Q3(vert, y, q.y1);
      Q3(vert, color, color);
      Q3(vert, u, q.s0);
      Q3(vert, v, q.t1);

      vert += 6;
    }

    ++text;
  }
}

void GLBackend::RenderBox2D(int x0, int y0, int x1, int y1, uint32_t color,
                            BoxType type) {
  Vertex2D *vertex = AllocVertices2D(
      {GL_TRIANGLES, 0, BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA, 0}, 6);

  if (type == BOX_FLAT) {
    CHECK(x0 <= x1);
    CHECK(y0 <= y1);

    // convert color from argb -> abgr
    color = (color & 0xff000000) | ((color & 0xff) << 16) | (color & 0xff00) |
            ((color & 0xff0000) >> 16);

    Q0(vertex, x, (float)x0);
    Q0(vertex, y, (float)y0);
    Q0(vertex, color, color);
    Q1(vertex, x, (float)x1);
    Q1(vertex, y, (float)y0);
    Q1(vertex, color, color);
    Q2(vertex, x, (float)x1);
    Q2(vertex, y, (float)y1);
    Q2(vertex, color, color);
    Q3(vertex, x, (float)x0);
    Q3(vertex, y, (float)y1);
    Q3(vertex, color, color);
  } else {
    uint32_t a = (color & 0xff000000) >> 24;
    uint32_t r = (color & 0xff0000) >> 16;
    uint32_t g = (color & 0xff00) >> 8;
    uint32_t b = color & 0xff;
    uint32_t max = std::max(std::max(std::max(r, g), b), 30u);
    uint32_t min = std::min(std::min(std::min(r, g), b), 180u);

    uint32_t r0 = 0xff & ((r + max) / 2);
    uint32_t g0 = 0xff & ((g + max) / 2);
    uint32_t b0 = 0xff & ((b + max) / 2);
    uint32_t r1 = 0xff & ((r + min) / 2);
    uint32_t g1 = 0xff & ((g + min) / 2);
    uint32_t b1 = 0xff & ((b + min) / 2);
    uint32_t color0 = (a << 24) | (b0 << 16) | (g0 << 8) | r0;
    uint32_t color1 = (a << 24) | (b1 << 16) | (g1 << 8) | r1;

    Q0(vertex, x, (float)x0);
    Q0(vertex, y, (float)y0);
    Q0(vertex, color, color0);
    Q1(vertex, x, (float)x1);
    Q1(vertex, y, (float)y0);
    Q1(vertex, color, color0);
    Q2(vertex, x, (float)x1);
    Q2(vertex, y, (float)y1);
    Q2(vertex, color, color1);
    Q3(vertex, x, (float)x0);
    Q3(vertex, y, (float)y1);
    Q3(vertex, color, color1);
  }
}

void GLBackend::RenderLine2D(float *verts, int num_verts, uint32_t color) {
  if (!num_verts) {
    return;
  }

  Vertex2D *vertex = AllocVertices2D(
      {GL_LINES, 0, BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA, 0},
      2 * (num_verts - 1));

  // convert color from argb -> abgr
  color = (color & 0xff000000) | ((color & 0xff) << 16) | (color & 0xff00) |
          ((color & 0xff0000) >> 16);

  for (int i = 0; i < num_verts - 1; ++i) {
    vertex[0].x = verts[i * 2];
    vertex[0].y = verts[i * 2 + 1];
    vertex[0].color = color;
    vertex[1].x = verts[(i + 1) * 2];
    vertex[1].y = verts[(i + 1) * 2 + 1];
    vertex[1].color = color;
    vertex += 2;
  }
}

void GLBackend::RenderSurfaces(const Eigen::Matrix4f &projection,
                               const Surface *surfs, int num_surfs,
                               const Vertex *verts, int num_verts,
                               const int *sorted_surfs) {
  // transpose to column-major for OpenGL
  Eigen::Matrix4f transposed = projection.transpose();

  glBindBuffer(GL_ARRAY_BUFFER, ta_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * num_verts, verts,
               GL_STATIC_DRAW);

  BindVAO(ta_vao_);

  for (int i = 0; i < num_surfs; i++) {
    const Surface *surf = &surfs[sorted_surfs[i]];

    SetDepthMask(surf->depth_write);
    SetDepthFunc(surf->depth_func);
    SetCullFace(surf->cull);
    SetBlendFunc(surf->src_blend, surf->dst_blend);

    // TODO use surf->shade to select correct shader

    BindProgram(&ta_program_);
    glUniformMatrix4fv(GetUniform(UNIFORM_MODELVIEWPROJECTIONMATRIX), 1,
                       GL_FALSE, transposed.data());
    glUniform1i(GetUniform(UNIFORM_DIFFUSEMAP), MAP_DIFFUSE);

    BindTexture(MAP_DIFFUSE,
                surf->texture ? textures_[surf->texture] : white_tex_);
    glDrawArrays(GL_TRIANGLE_STRIP, surf->first_vert, surf->num_verts);
  }
}

void GLBackend::EndFrame() {
  Flush2D();

  ctx_.GLSwapBuffers();
}

void GLBackend::InitFramebuffers() {
  CHECK_GT(state_.ta_width, 0);
  CHECK_GT(state_.ta_height, 0);

  glGenFramebuffers(1, &fb_ta_);
  glGenTextures(1, &fb_ta_color_);
  glGenRenderbuffers(1, &fb_ta_depth_);

  glBindTexture(GL_TEXTURE_2D, fb_ta_color_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, state_.ta_width, state_.ta_height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_2D, 0);

  glBindRenderbuffer(GL_RENDERBUFFER, fb_ta_depth_);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, state_.ta_width,
                        state_.ta_height);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, fb_ta_);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                         GL_TEXTURE_2D, fb_ta_color_, 0);
  glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, fb_ta_depth_);
  GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
  CHECK_EQ(status, GL_FRAMEBUFFER_COMPLETE);
  Clear(0.0f, 0.0f, 0.0f, 1.0f);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLBackend::DestroyFramebuffers() {
  glDeleteFramebuffers(1, &fb_ta_);
  glDeleteTextures(1, &fb_ta_color_);
  glDeleteRenderbuffers(1, &fb_ta_depth_);
}

void GLBackend::InitTextures() {
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
  glDeleteTextures(1, &white_tex_);

  for (int i = 1; i < MAX_TEXTURES; i++) {
    if (!textures_[i]) {
      continue;
    }
    glDeleteTextures(1, &textures_[i]);
  }
}

void GLBackend::InitShaders() {
  if (!CompileProgram(&ta_program_, nullptr, ta_vp, ta_fp)) {
    LOG_FATAL("Failed to compile ta shader.");
  }

  if (!CompileProgram(&ui_program_, nullptr, ui_vp, ui_fp)) {
    LOG_FATAL("Failed to compile ui shader.");
  }
}

void GLBackend::DestroyShaders() {
  DestroyProgram(&ta_program_);
  DestroyProgram(&ui_program_);
}

void GLBackend::InitVertexBuffers() {
  //
  // UI vao
  //
  glGenVertexArrays(1, &ui_vao_);
  glBindVertexArray(ui_vao_);

  glGenBuffers(1, &ui_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);

  // xy
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, 0, sizeof(Vertex2D),
                        (void *)offsetof(Vertex2D, x));

  // color
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex2D),
                        (void *)offsetof(Vertex2D, color));

  // texcoord
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, 0, sizeof(Vertex2D),
                        (void *)offsetof(Vertex2D, u));

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  //
  // TA vao
  //
  glGenVertexArrays(1, &ta_vao_);
  glBindVertexArray(ta_vao_);

  glGenBuffers(1, &ta_vbo_);
  glBindBuffer(GL_ARRAY_BUFFER, ta_vbo_);

  glGenBuffers(1, &ta_ibo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ta_ibo_);

  // xyz
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, 0, sizeof(Vertex),
                        (void *)offsetof(Vertex, xyz));

  // color
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(Vertex),
                        (void *)offsetof(Vertex, color));

  // offset color
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(Vertex),
                        (void *)offsetof(Vertex, offset_color));

  // texcoord
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 2, GL_FLOAT, 0, sizeof(Vertex),
                        (void *)offsetof(Vertex, uv));

  glBindVertexArray(0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void GLBackend::DestroyVertexBuffers() {
  glDeleteBuffers(1, &ui_vbo_);
  glDeleteVertexArrays(1, &ui_vao_);

  glDeleteBuffers(1, &ta_ibo_);
  glDeleteBuffers(1, &ta_vbo_);
  glDeleteVertexArrays(1, &ta_vao_);
}

void GLBackend::DestroyFonts() {
  for (auto it : fonts_) {
    delete it.second;
  }
}

void GLBackend::SetupDefaultState() {
  glEnable(GL_SCISSOR_TEST);
  glDrawBuffer(GL_FRONT_AND_BACK);
  Clear(0.0f, 0.0f, 0.0f, 1.0f);
  glDrawBuffer(GL_BACK);
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

const BakedFont *GLBackend::GetFont(float point_size) {
  static const int FONT_TEXTURE_SIZE = 512;
  static const unsigned char *ttf_data = inconsolata_ttf;

  auto it = fonts_.find(point_size);
  if (it != fonts_.end()) {
    return it->second;
  }

  std::unique_ptr<BakedFont> font(new BakedFont());
  font->tw = FONT_TEXTURE_SIZE;
  font->th = FONT_TEXTURE_SIZE;

  // load the font ourself in order to get the ascent info
  stbtt_fontinfo f;
  if (!stbtt_InitFont(&f, ttf_data, 0)) {
    LOG_WARNING("Failed to initialize font");
    return nullptr;
  }
  int ascent;
  stbtt_GetFontVMetrics(&f, &ascent, nullptr, nullptr);
  font->ascent = (float)ascent * stbtt_ScaleForPixelHeight(&f, point_size);

  // bake the font into the bitmap
  unsigned char bitmap[FONT_TEXTURE_SIZE * FONT_TEXTURE_SIZE];
  stbtt_pack_context pc;
  stbtt_PackBegin(&pc, bitmap, FONT_TEXTURE_SIZE, FONT_TEXTURE_SIZE, 0, 1,
                  NULL);
  stbtt_PackSetOversampling(&pc, 2, 2);
  if (!stbtt_PackFontRange(&pc, ttf_data, 0, point_size, 32, 127,
                           font->chars + 32)) {
    LOG_WARNING("Failed to pack font");
    return nullptr;
  }
  stbtt_PackEnd(&pc);

  // generate gl texture for bitmap
  GLuint texid;
  glGenTextures(1, &texid);
  glBindTexture(GL_TEXTURE_2D, texid);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  GLint swizzle_mask[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
  glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle_mask);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_TEXTURE_SIZE, FONT_TEXTURE_SIZE, 0,
               GL_RED, GL_UNSIGNED_BYTE, bitmap);
  glBindTexture(GL_TEXTURE_2D, 0);
  font->texture = texid;

  // insert into cache (map now takes ownership)
  auto pair = fonts_.insert(std::make_pair(point_size, font.release()));
  CHECK(pair.second);
  return (pair.first)->second;
}

Eigen::Matrix4f GLBackend::Ortho2D() {
  Eigen::Matrix4f p = Eigen::Matrix4f::Identity();
  p(0, 0) = 2.0f / (float)state_.video_width;
  p(1, 1) = -2.0f / (float)state_.video_height;
  p(0, 3) = -1.0;
  p(1, 3) = 1.0;
  p(2, 2) = 0;
  return p;
}

Vertex2D *GLBackend::AllocVertices2D(const Surface2D &desc, int count) {
  if (num_verts2d_ + count > MAX_2D_VERTICES) {
    Flush2D();
  }

  CHECK(num_verts2d_ + count <= MAX_2D_VERTICES);
  uint32_t first_vert = num_verts2d_;
  num_verts2d_ += count;

  // try to batch with the last surface if possible
  if (num_surfs2d_) {
    Surface2D &last_surf = surfs2d_[num_surfs2d_ - 1];

    if (last_surf.prim_type == desc.prim_type &&
        last_surf.texture == desc.texture &&
        last_surf.src_blend == desc.src_blend &&
        last_surf.dst_blend == desc.dst_blend) {
      last_surf.num_verts += count;
      return &verts2d_[first_vert];
    }
  }

  // else, allocate a new surface
  CHECK(num_surfs2d_ < MAX_2D_SURFACES);
  Surface2D &next_surf = surfs2d_[num_surfs2d_];
  next_surf.prim_type = desc.prim_type;
  next_surf.texture = desc.texture;
  next_surf.src_blend = desc.src_blend;
  next_surf.dst_blend = desc.dst_blend;
  next_surf.num_verts = count;
  num_surfs2d_++;
  return &verts2d_[first_vert];
}

void GLBackend::Flush2D() {
  if (!num_surfs2d_) {
    return;
  }

  Eigen::Matrix4f ortho = Ortho2D();
  Eigen::Matrix4f projection = ortho.transpose();

  glBindBuffer(GL_ARRAY_BUFFER, ui_vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex2D) * num_verts2d_, verts2d_,
               GL_STATIC_DRAW);

  SetDepthMask(false);
  SetDepthFunc(DEPTH_NONE);
  SetCullFace(CULL_NONE);

  BindVAO(ui_vao_);
  BindProgram(&ui_program_);
  glUniformMatrix4fv(GetUniform(UNIFORM_MODELVIEWPROJECTIONMATRIX), 1, GL_FALSE,
                     projection.data());
  glUniform1i(GetUniform(UNIFORM_DIFFUSEMAP), MAP_DIFFUSE);

  int offset = 0;
  for (int i = 0; i < num_surfs2d_; ++i) {
    int count = surfs2d_[i].num_verts;
    BindTexture(MAP_DIFFUSE,
                surfs2d_[i].texture ? surfs2d_[i].texture : white_tex_);
    SetBlendFunc(surfs2d_[i].src_blend, surfs2d_[i].dst_blend);
    glDrawArrays(surfs2d_[i].prim_type, offset, count);
    offset += count;
  }

  num_verts2d_ = 0;
  num_surfs2d_ = 0;
}
