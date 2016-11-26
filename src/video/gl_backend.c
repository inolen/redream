#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include "core/assert.h"
#include "core/profiler.h"
#include "core/string.h"
#include "ui/nuklear.h"
#include "ui/window.h"
#include "video/backend.h"

#define MAX_TEXTURES 8192

enum texture_map {
  MAP_DIFFUSE,
};

enum uniform_attr {
  UNIFORM_MODELVIEWPROJECTIONMATRIX,
  UNIFORM_DIFFUSEMAP,
  UNIFORM_NUM_UNIFORMS
};

struct shader_program {
  GLuint program;
  GLuint vertex_shader;
  GLuint fragment_shader;
  GLint uniforms[UNIFORM_NUM_UNIFORMS];
};

struct video_backend {
  struct window *window;
  struct window_listener listener;

  SDL_GLContext ctx;
  int debug_wireframe;

  // resources
  GLuint textures[MAX_TEXTURES];
  GLuint white_tex;

  struct shader_program ta_program;
  struct shader_program ui_program;

  GLuint ta_vao;
  GLuint ta_vbo;
  GLuint ui_vao;
  GLuint ui_vbo;
  GLuint ui_ibo;
  bool ui_use_ibo;

  // current gl state
  bool scissor_test;
  bool depth_mask;
  enum depth_func depth_func;
  enum cull_face cull_face;
  enum blend_func src_blend;
  enum blend_func dst_blend;
  GLuint current_vao;
  int vertex_attribs;
  struct shader_program *current_program;
};

#include "video/ta.glsl"
#include "video/ui.glsl"

static const int GLSL_VERSION = 330;

// must match order of enum uniform_attr enum
static const char *uniform_names[] = {"u_mvp",  //
                                      "u_diffuse_map"};

static GLenum filter_funcs[] = {
    GL_NEAREST,                // FILTER_NEAREST
    GL_LINEAR,                 // FILTER_BILINEAR
    GL_NEAREST_MIPMAP_LINEAR,  // FILTER_NEAREST + mipmaps
    GL_LINEAR_MIPMAP_LINEAR    // FILTER_BILINEAR + mipmaps
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

static void video_set_scissor_test(struct video_backend *video, bool enabled) {
  if (video->scissor_test == enabled) {
    return;
  }

  video->scissor_test = enabled;

  if (enabled) {
    glEnable(GL_SCISSOR_TEST);
  } else {
    glDisable(GL_SCISSOR_TEST);
  }
}

static void video_set_scissor_clip(struct video_backend *video, int x, int y,
                                   int width, int height) {
  glScissor(x, y, width, height);
}

static void video_set_depth_mask(struct video_backend *video, bool enabled) {
  if (video->depth_mask == enabled) {
    return;
  }

  video->depth_mask = enabled;

  glDepthMask(enabled ? 1 : 0);
}

static void video_set_depth_func(struct video_backend *video,
                                 enum depth_func fn) {
  if (video->depth_func == fn) {
    return;
  }

  video->depth_func = fn;

  if (fn == DEPTH_NONE) {
    glDisable(GL_DEPTH_TEST);
  } else {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(depth_funcs[fn]);
  }
}

static void video_set_cull_face(struct video_backend *video,
                                enum cull_face fn) {
  if (video->cull_face == fn) {
    return;
  }

  video->cull_face = fn;

  if (fn == CULL_NONE) {
    glDisable(GL_CULL_FACE);
  } else {
    glEnable(GL_CULL_FACE);
    glCullFace(cull_face[fn]);
  }
}

static void video_set_blend_func(struct video_backend *video,
                                 enum blend_func src_fn,
                                 enum blend_func dst_fn) {
  if (video->src_blend == src_fn && video->dst_blend == dst_fn) {
    return;
  }

  video->src_blend = src_fn;
  video->dst_blend = dst_fn;

  if (src_fn == BLEND_NONE || dst_fn == BLEND_NONE) {
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendFunc(blend_funcs[src_fn], blend_funcs[dst_fn]);
  }
}

static void video_bind_vao(struct video_backend *video, GLuint vao) {
  if (video->current_vao == vao) {
    return;
  }

  video->current_vao = vao;

  glBindVertexArray(vao);
}

static void video_bind_program(struct video_backend *video,
                               struct shader_program *program) {
  if (video->current_program == program) {
    return;
  }

  video->current_program = program;

  glUseProgram(program ? program->program : 0);
}

void video_bind_texture(struct video_backend *video, enum texture_map map,
                        GLuint tex) {
  glActiveTexture(GL_TEXTURE0 + map);
  glBindTexture(GL_TEXTURE_2D, tex);
}

static GLint video_get_uniform(struct video_backend *video,
                               enum uniform_attr attr) {
  return video->current_program->uniforms[attr];
}

static void video_print_shader_log(GLuint shader) {
  int max_length, length;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_length);

  char *info_log = malloc(max_length);
  glGetShaderInfoLog(shader, max_length, &length, info_log);
  LOG_INFO(info_log);
  free(info_log);
}

static bool video_compile_shader(const char *source, GLenum shader_type,
                                 GLuint *shader) {
  size_t sourceLength = strlen(source);

  *shader = glCreateShader(shader_type);
  glShaderSource(*shader, 1, (const GLchar **)&source,
                 (const GLint *)&sourceLength);
  glCompileShader(*shader);

  GLint compiled;
  glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);

  if (!compiled) {
    video_print_shader_log(*shader);
    glDeleteShader(*shader);
    return false;
  }

  return true;
}

static void video_destroy_program(struct shader_program *program) {
  if (program->vertex_shader > 0) {
    glDeleteShader(program->vertex_shader);
  }

  if (program->fragment_shader > 0) {
    glDeleteShader(program->fragment_shader);
  }

  glDeleteProgram(program->program);
}

static bool video_compile_program(struct shader_program *program,
                                  const char *header, const char *vertex_source,
                                  const char *fragment_source) {
  char buffer[16384] = {0};

  memset(program, 0, sizeof(*program));
  program->program = glCreateProgram();

  if (vertex_source) {
    snprintf(buffer, sizeof(buffer) - 1, "#version %d\n%s%s", GLSL_VERSION,
             header ? header : "", vertex_source);
    buffer[sizeof(buffer) - 1] = 0;

    if (!video_compile_shader(buffer, GL_VERTEX_SHADER,
                              &program->vertex_shader)) {
      video_destroy_program(program);
      return false;
    }

    glAttachShader(program->program, program->vertex_shader);
  }

  if (fragment_source) {
    snprintf(buffer, sizeof(buffer) - 1, "#version %d\n%s%s", GLSL_VERSION,
             header ? header : "", fragment_source);
    buffer[sizeof(buffer) - 1] = 0;

    if (!video_compile_shader(buffer, GL_FRAGMENT_SHADER,
                              &program->fragment_shader)) {
      video_destroy_program(program);
      return false;
    }

    glAttachShader(program->program, program->fragment_shader);
  }

  glLinkProgram(program->program);

  GLint linked;
  glGetProgramiv(program->program, GL_LINK_STATUS, &linked);

  if (!linked) {
    video_destroy_program(program);
    return false;
  }

  for (int i = 0; i < UNIFORM_NUM_UNIFORMS; i++) {
    program->uniforms[i] =
        glGetUniformLocation(program->program, uniform_names[i]);
  }

  return true;
}

static bool video_init_context(struct video_backend *video) {
  // need at least a 3.3 core context for our shaders
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  // request a 24-bit depth buffer. 16-bits isn't enough precision when
  // unprojecting dreamcast coordinates, see tr_proj_mat
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  video->ctx = SDL_GL_CreateContext(video->window->handle);
  if (!video->ctx) {
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

static void video_destroy_context(struct video_backend *video) {
  if (!video->ctx) {
    return;
  }

  SDL_GL_DeleteContext(video->ctx);
  video->ctx = NULL;
}

static void video_create_textures(struct video_backend *video) {
  uint8_t pixels[64 * 64 * 4];
  memset(pixels, 0xff, sizeof(pixels));
  glGenTextures(1, &video->white_tex);
  glBindTexture(GL_TEXTURE_2D, video->white_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
}

static void video_destroy_textures(struct video_backend *video) {
  if (!video->ctx) {
    return;
  }

  glDeleteTextures(1, &video->white_tex);

  for (int i = 1; i < MAX_TEXTURES; i++) {
    if (!video->textures[i]) {
      continue;
    }
    glDeleteTextures(1, &video->textures[i]);
  }
}

static void video_create_shaders(struct video_backend *video) {
  if (!video_compile_program(&video->ta_program, NULL, ta_vp, ta_fp)) {
    LOG_FATAL("Failed to compile ta shader.");
  }

  if (!video_compile_program(&video->ui_program, NULL, ui_vp, ui_fp)) {
    LOG_FATAL("Failed to compile ui shader.");
  }
}

static void video_destroy_shaders(struct video_backend *video) {
  if (!video->ctx) {
    return;
  }

  video_destroy_program(&video->ta_program);
  video_destroy_program(&video->ui_program);
}

static void video_create_vertex_buffers(struct video_backend *video) {
  //
  // UI vao
  //
  glGenVertexArrays(1, &video->ui_vao);
  glBindVertexArray(video->ui_vao);

  glGenBuffers(1, &video->ui_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, video->ui_vbo);

  glGenBuffers(1, &video->ui_ibo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, video->ui_ibo);

  // xy
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex2d),
                        (void *)offsetof(struct vertex2d, xy));

  // texcoord
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex2d),
                        (void *)offsetof(struct vertex2d, uv));

  // color
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                        sizeof(struct vertex2d),
                        (void *)offsetof(struct vertex2d, color));

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  //
  // TA vao
  //
  glGenVertexArrays(1, &video->ta_vao);
  glBindVertexArray(video->ta_vao);

  glGenBuffers(1, &video->ta_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, video->ta_vbo);

  // xyz
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                        (void *)offsetof(struct vertex, xyz));

  // texcoord
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                        (void *)offsetof(struct vertex, uv));

  // color
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex),
                        (void *)offsetof(struct vertex, color));

  // offset color
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex),
                        (void *)offsetof(struct vertex, offset_color));

  glBindVertexArray(0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void video_destroy_vertex_buffers(struct video_backend *video) {
  if (!video->ctx) {
    return;
  }

  glDeleteBuffers(1, &video->ui_ibo);
  glDeleteBuffers(1, &video->ui_vbo);
  glDeleteVertexArrays(1, &video->ui_vao);

  glDeleteBuffers(1, &video->ta_vbo);
  glDeleteVertexArrays(1, &video->ta_vao);
}

static void video_set_initial_state(struct video_backend *video) {
  video_set_depth_mask(video, true);
  video_set_depth_func(video, DEPTH_NONE);
  video_set_cull_face(video, CULL_BACK);
  video_set_blend_func(video, BLEND_NONE, BLEND_NONE);
}

static void video_debug_menu(void *data, struct nk_context *ctx) {
  struct video_backend *video = data;

  nk_layout_row_push(ctx, 50.0f);

  if (nk_menu_begin_label(ctx, "VIDEO", NK_TEXT_LEFT,
                          nk_vec2(140.0f, 200.0f))) {
    nk_layout_row_dynamic(ctx, DEBUG_MENU_HEIGHT, 1);
    nk_checkbox_label(ctx, "wireframe", &video->debug_wireframe);
    nk_menu_end(ctx);
  }
}

void video_begin_surfaces(struct video_backend *video, const float *projection,
                          const struct vertex *verts, int num_verts) {
  glBindBuffer(GL_ARRAY_BUFFER, video->ta_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(struct vertex) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  video_bind_vao(video, video->ta_vao);
  video_bind_program(video, &video->ta_program);
  glUniformMatrix4fv(
      video_get_uniform(video, UNIFORM_MODELVIEWPROJECTIONMATRIX), 1, GL_FALSE,
      projection);
  glUniform1i(video_get_uniform(video, UNIFORM_DIFFUSEMAP), MAP_DIFFUSE);

  if (video->debug_wireframe) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
}

void video_draw_surface(struct video_backend *video,
                        const struct surface *surf) {
  video_set_depth_mask(video, surf->depth_write);
  video_set_depth_func(video, surf->depth_func);
  video_set_cull_face(video, surf->cull);
  video_set_blend_func(video, surf->src_blend, surf->dst_blend);

  // TODO use surf->shade to select correct shader

  video_bind_texture(video, MAP_DIFFUSE, surf->texture
                                             ? video->textures[surf->texture]
                                             : video->white_tex);
  glDrawArrays(GL_TRIANGLE_STRIP, surf->first_vert, surf->num_verts);
}

void video_end_surfaces(struct video_backend *video) {
  if (video->debug_wireframe) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
}

void video_begin_surfaces2d(struct video_backend *video,
                            const struct vertex2d *verts, int num_verts,
                            uint16_t *indices, int num_indices) {
  glBindBuffer(GL_ARRAY_BUFFER, video->ui_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(struct vertex2d) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  if (indices) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, video->ui_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * num_indices,
                 indices, GL_DYNAMIC_DRAW);
    video->ui_use_ibo = true;
  } else {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, -1);
    video->ui_use_ibo = false;
  }
}

void video_draw_surface2d(struct video_backend *video,
                          const struct surface2d *surf) {
  if (surf->scissor) {
    video_set_scissor_test(video, true);
    video_set_scissor_clip(
        video, (int)surf->scissor_rect[0], (int)surf->scissor_rect[1],
        (int)surf->scissor_rect[2], (int)surf->scissor_rect[3]);
  } else {
    video_set_scissor_test(video, false);
  }

  video_set_blend_func(video, surf->src_blend, surf->dst_blend);
  video_bind_texture(video, MAP_DIFFUSE, surf->texture
                                             ? video->textures[surf->texture]
                                             : video->white_tex);

  if (video->ui_use_ibo) {
    glDrawElements(prim_types[surf->prim_type], surf->num_verts,
                   GL_UNSIGNED_SHORT,
                   (void *)(intptr_t)(sizeof(uint16_t) * surf->first_vert));
  } else {
    glDrawArrays(prim_types[surf->prim_type], surf->first_vert,
                 surf->num_verts);
  }
}

void video_end_surfaces2d(struct video_backend *video) {}

void video_begin_ortho(struct video_backend *video) {
  float ortho[16];

  ortho[0] = 2.0f / (float)video->window->width;
  ortho[4] = 0.0f;
  ortho[8] = 0.0f;
  ortho[12] = -1.0f;

  ortho[1] = 0.0f;
  ortho[5] = -2.0f / (float)video->window->height;
  ortho[9] = 0.0f;
  ortho[13] = 1.0f;

  ortho[2] = 0.0f;
  ortho[6] = 0.0f;
  ortho[10] = 0.0f;
  ortho[14] = 0.0f;

  ortho[3] = 0.0f;
  ortho[7] = 0.0f;
  ortho[11] = 0.0f;
  ortho[15] = 1.0f;

  video_set_depth_mask(video, false);
  video_set_depth_func(video, DEPTH_NONE);
  video_set_cull_face(video, CULL_NONE);

  video_bind_vao(video, video->ui_vao);
  video_bind_program(video, &video->ui_program);
  glUniformMatrix4fv(
      video_get_uniform(video, UNIFORM_MODELVIEWPROJECTIONMATRIX), 1, GL_FALSE,
      ortho);
  glUniform1i(video_get_uniform(video, UNIFORM_DIFFUSEMAP), MAP_DIFFUSE);
}

void video_end_ortho(struct video_backend *video) {
  video_set_scissor_test(video, false);
}

void video_begin_frame(struct video_backend *video) {
  video_set_depth_mask(video, true);

  glViewport(0, 0, video->window->width, video->window->height);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void video_end_frame(struct video_backend *video) {
  SDL_GL_SwapWindow(video->window->handle);
}

texture_handle_t video_create_texture(
    struct video_backend *video, enum pxl_format format,
    enum filter_mode filter, enum wrap_mode wrap_u, enum wrap_mode wrap_v,
    bool mipmaps, int width, int height, const uint8_t *buffer) {
  // FIXME worth speeding up?
  texture_handle_t handle;
  for (handle = 1; handle < MAX_TEXTURES; handle++) {
    if (!video->textures[handle]) {
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

  GLuint *gltex = &video->textures[handle];
  glGenTextures(1, gltex);
  glBindTexture(GL_TEXTURE_2D, *gltex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  filter_funcs[mipmaps * NUM_FILTER_MODES + filter]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_funcs[filter]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_modes[wrap_u]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_modes[wrap_v]);
  glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, width, height, 0, internal_fmt,
               pixel_fmt, buffer);

  if (mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  glBindTexture(GL_TEXTURE_2D, 0);

  return handle;
}

void video_destroy_texture(struct video_backend *video,
                           texture_handle_t handle) {
  GLuint *gltex = &video->textures[handle];
  glDeleteTextures(1, gltex);
  *gltex = 0;
}

struct video_backend *video_create(struct window *window) {
  struct video_backend *video =
      (struct video_backend *)calloc(1, sizeof(struct video_backend));
  video->window = window;
  video->listener = (struct window_listener){
      video, NULL, &video_debug_menu, NULL, NULL, NULL, NULL, {0}};

  win_add_listener(video->window, &video->listener);

  if (!video_init_context(video)) {
    video_destroy(video);
    return NULL;
  }

  video_create_textures(video);
  video_create_shaders(video);
  video_create_vertex_buffers(video);
  video_set_initial_state(video);

  return video;
}

void video_destroy(struct video_backend *video) {
  video_destroy_vertex_buffers(video);
  video_destroy_shaders(video);
  video_destroy_textures(video);
  video_destroy_context(video);
  win_remove_listener(video->window, &video->listener);
  free(video);
}
