#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include "core/assert.h"
#include "core/profiler.h"
#include "core/string.h"
#include "ui/nuklear.h"
#include "ui/window.h"
#include "video/render_backend.h"

#define MAX_FRAMEBUFFERS 8
#define MAX_TEXTURES 8192

enum texture_map {
  MAP_DIFFUSE,
};

enum uniform_attr {
  UNIFORM_MVP = 0,
  UNIFORM_DIFFUSE = 1,
  UNIFORM_PT_ALPHA_REF = 2,
  UNIFORM_NUM_UNIFORMS = 3
};

static const char *uniform_names[] = {
    "u_mvp", "u_diffuse", "u_pt_alpha_ref",
};

enum shader_attr {
  /* shade attributes are mutually exclusive, so they don't use unique bits */
  ATTR_SHADE_DECAL = 0x0,
  ATTR_SHADE_MODULATE = 0x1,
  ATTR_SHADE_DECAL_ALPHA = 0x2,
  ATTR_SHADE_MODULATE_ALPHA = 0x3,
  ATTR_SHADE_MASK = 0x3,
  /* remaining attributes can all be combined together */
  ATTR_TEXTURE = 0x4,
  ATTR_IGNORE_ALPHA = 0x8,
  ATTR_IGNORE_TEXTURE_ALPHA = 0x10,
  ATTR_OFFSET_COLOR = 0x20,
  ATTR_PT_ALPHA_TEST = 0x40,
  ATTR_COUNT = 0x80
};

struct shader_program {
  GLuint program;
  GLuint vertex_shader;
  GLuint fragment_shader;
  GLint uniforms[UNIFORM_NUM_UNIFORMS];
  uint64_t uniform_token;
};

struct framebuffer {
  GLuint fbo;
  GLuint color_component;
  GLuint depth_component;
};

struct texture {
  GLuint texture;
};

struct render_backend {
  struct window *window;

  glcontext_t ctx;

  /* resources */
  struct framebuffer framebuffers[MAX_FRAMEBUFFERS];
  struct texture textures[MAX_TEXTURES];
  struct texture white;

  struct shader_program ta_programs[ATTR_COUNT];
  struct shader_program ui_program;

  GLuint ta_vao;
  GLuint ta_vbo;
  GLuint ui_vao;
  GLuint ui_vbo;
  GLuint ui_ibo;
  int ui_use_ibo;

  /* begin_surfaces / draw_surfaces / end_surfaces uniform state */
  uint64_t uniform_token;
  const float *uniform_mvp;

  /* current gl state */
  int scissor_test;
  int depth_mask;
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

static GLenum filter_funcs[] = {
    GL_NEAREST,               /* FILTER_NEAREST */
    GL_LINEAR,                /* FILTER_BILINEAR */
    GL_NEAREST_MIPMAP_LINEAR, /* FILTER_NEAREST + mipmaps */
    GL_LINEAR_MIPMAP_LINEAR   /* FILTER_BILINEAR + mipmaps */
};

static GLenum wrap_modes[] = {
    GL_REPEAT,         /* WRAP_REPEAT */
    GL_CLAMP_TO_EDGE,  /* WRAP_CLAMP_TO_EDGE */
    GL_MIRRORED_REPEAT /* WRAP_MIRRORED_REPEAT */
};

static GLenum depth_funcs[] = {
    GL_NONE,     /* DEPTH_NONE */
    GL_NEVER,    /* DEPTH_NEVER */
    GL_LESS,     /* DEPTH_LESS */
    GL_EQUAL,    /* DEPTH_EQUAL */
    GL_LEQUAL,   /* DEPTH_LEQUAL */
    GL_GREATER,  /* DEPTH_GREATER */
    GL_NOTEQUAL, /* DEPTH_NEQUAL */
    GL_GEQUAL,   /* DEPTH_GEQUAL */
    GL_ALWAYS    /* DEPTH_ALWAYS */
};

static GLenum cull_face[] = {
    GL_NONE,  /* CULL_NONE */
    GL_FRONT, /* CULL_FRONT */
    GL_BACK   /* CULL_BACK */
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
    GL_TRIANGLES, /* PRIM_TRIANGLES */
    GL_LINES,     /* PRIM_LINES */
};

static void r_set_scissor_test(struct render_backend *r, int enabled) {
  if (r->scissor_test == enabled) {
    return;
  }

  r->scissor_test = enabled;

  if (enabled) {
    glEnable(GL_SCISSOR_TEST);
  } else {
    glDisable(GL_SCISSOR_TEST);
  }
}

static void r_set_scissor_clip(struct render_backend *r, int x, int y,
                               int width, int height) {
  glScissor(x, y, width, height);
}

static void r_set_depth_mask(struct render_backend *r, int enabled) {
  if (r->depth_mask == enabled) {
    return;
  }

  r->depth_mask = enabled;

  glDepthMask(enabled ? 1 : 0);
}

static void r_set_depth_func(struct render_backend *r, enum depth_func fn) {
  if (r->depth_func == fn) {
    return;
  }

  r->depth_func = fn;

  if (fn == DEPTH_NONE) {
    glDisable(GL_DEPTH_TEST);
  } else {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(depth_funcs[fn]);
  }
}

static void r_set_cull_face(struct render_backend *r, enum cull_face fn) {
  if (r->cull_face == fn) {
    return;
  }

  r->cull_face = fn;

  if (fn == CULL_NONE) {
    glDisable(GL_CULL_FACE);
  } else {
    glEnable(GL_CULL_FACE);
    glCullFace(cull_face[fn]);
  }
}

static void r_set_blend_func(struct render_backend *r, enum blend_func src_fn,
                             enum blend_func dst_fn) {
  if (r->src_blend == src_fn && r->dst_blend == dst_fn) {
    return;
  }

  r->src_blend = src_fn;
  r->dst_blend = dst_fn;

  if (src_fn == BLEND_NONE || dst_fn == BLEND_NONE) {
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendFunc(blend_funcs[src_fn], blend_funcs[dst_fn]);
  }
}

static void r_bind_vao(struct render_backend *r, GLuint vao) {
  if (r->current_vao == vao) {
    return;
  }

  r->current_vao = vao;

  glBindVertexArray(vao);
}

static void r_bind_program(struct render_backend *r,
                           struct shader_program *program) {
  if (r->current_program == program) {
    return;
  }

  r->current_program = program;

  glUseProgram(program ? program->program : 0);
}

void r_bind_texture(struct render_backend *r, enum texture_map map,
                    GLuint tex) {
  glActiveTexture(GL_TEXTURE0 + map);
  glBindTexture(GL_TEXTURE_2D, tex);
}

static GLint r_get_uniform(struct render_backend *r, enum uniform_attr attr) {
  return r->current_program->uniforms[attr];
}

static void r_print_shader_log(GLuint shader) {
  int max_length, length;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_length);

  char *info_log = malloc(max_length);
  glGetShaderInfoLog(shader, max_length, &length, info_log);
  LOG_INFO(info_log);
  free(info_log);
}

static int r_compile_shader(const char *source, GLenum shader_type,
                            GLuint *shader) {
  size_t sourceLength = strlen(source);

  *shader = glCreateShader(shader_type);
  glShaderSource(*shader, 1, (const GLchar **)&source,
                 (const GLint *)&sourceLength);
  glCompileShader(*shader);

  GLint compiled;
  glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);

  if (!compiled) {
    r_print_shader_log(*shader);
    glDeleteShader(*shader);
    return 0;
  }

  return 1;
}

static void r_destroy_program(struct shader_program *program) {
  if (program->vertex_shader > 0) {
    glDeleteShader(program->vertex_shader);
  }

  if (program->fragment_shader > 0) {
    glDeleteShader(program->fragment_shader);
  }

  glDeleteProgram(program->program);
}

static int r_compile_program(struct render_backend *r,
                             struct shader_program *program, const char *header,
                             const char *vertex_source,
                             const char *fragment_source) {
  char buffer[16384] = {0};

  memset(program, 0, sizeof(*program));
  program->program = glCreateProgram();

  if (vertex_source) {
    snprintf(buffer, sizeof(buffer) - 1,
             "#version 330\n"
             "%s%s",
             header ? header : "", vertex_source);
    buffer[sizeof(buffer) - 1] = 0;

    if (!r_compile_shader(buffer, GL_VERTEX_SHADER, &program->vertex_shader)) {
      r_destroy_program(program);
      return 0;
    }

    glAttachShader(program->program, program->vertex_shader);
  }

  if (fragment_source) {
    snprintf(buffer, sizeof(buffer) - 1,
             "#version 330\n"
             "%s%s",
             header ? header : "", fragment_source);
    buffer[sizeof(buffer) - 1] = 0;

    if (!r_compile_shader(buffer, GL_FRAGMENT_SHADER,
                          &program->fragment_shader)) {
      r_destroy_program(program);
      return 0;
    }

    glAttachShader(program->program, program->fragment_shader);
  }

  glLinkProgram(program->program);

  GLint linked;
  glGetProgramiv(program->program, GL_LINK_STATUS, &linked);

  if (!linked) {
    r_destroy_program(program);
    return 0;
  }

  for (int i = 0; i < UNIFORM_NUM_UNIFORMS; i++) {
    program->uniforms[i] =
        glGetUniformLocation(program->program, uniform_names[i]);
  }

  /* bind diffuse sampler once after compile, this currently never changes */
  r_bind_program(r, program);
  glUniform1i(r_get_uniform(r, UNIFORM_DIFFUSE), MAP_DIFFUSE);
  r_bind_program(r, NULL);

  return 1;
}

static void r_destroy_textures(struct render_backend *r) {
  if (!r->ctx) {
    return;
  }

  glDeleteTextures(1, &r->white.texture);

  for (int i = 1; i < MAX_TEXTURES; i++) {
    struct texture *tex = &r->textures[i];

    if (!tex->texture) {
      continue;
    }

    glDeleteTextures(1, &tex->texture);
  }
}

static void r_create_textures(struct render_backend *r) {
  uint8_t pixels[64 * 64 * 4];

  memset(pixels, 0xff, sizeof(pixels));
  glGenTextures(1, &r->white.texture);
  glBindTexture(GL_TEXTURE_2D, r->white.texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
}

static void r_destroy_shaders(struct render_backend *r) {
  if (!r->ctx) {
    return;
  }

  for (int i = 0; i < ATTR_COUNT; i++) {
    r_destroy_program(&r->ta_programs[i]);
  }

  r_destroy_program(&r->ui_program);
}

static void r_create_shaders(struct render_backend *r) {
  char header[1024];

  for (int i = 0; i < ATTR_COUNT; i++) {
    struct shader_program *program = &r->ta_programs[i];

    header[0] = 0;

    if ((i & ATTR_SHADE_MASK) == ATTR_SHADE_DECAL) {
      strcat(header, "#define SHADE_DECAL\n");
    }

    if ((i & ATTR_SHADE_MASK) == ATTR_SHADE_MODULATE) {
      strcat(header, "#define SHADE_MODULATE\n");
    }

    if ((i & ATTR_SHADE_MASK) == ATTR_SHADE_DECAL_ALPHA) {
      strcat(header, "#define SHADE_DECAL_ALPHA\n");
    }

    if ((i & ATTR_SHADE_MASK) == ATTR_SHADE_MODULATE_ALPHA) {
      strcat(header, "#define SHADE_MODULATE_ALPHA\n");
    }

    if (i & ATTR_TEXTURE) {
      strcat(header, "#define TEXTURE\n");
    }

    if (i & ATTR_IGNORE_ALPHA) {
      strcat(header, "#define IGNORE_ALPHA\n");
    }

    if (i & ATTR_IGNORE_TEXTURE_ALPHA) {
      strcat(header, "#define IGNORE_TEXTURE_ALPHA\n");
    }

    if (i & ATTR_OFFSET_COLOR) {
      strcat(header, "#define OFFSET_COLOR\n");
    }
    if (i & ATTR_PT_ALPHA_TEST) {
      strcat(header, "#define PT_ALPHA_TEST\n");
    }

    if (!r_compile_program(r, program, header, ta_vp, ta_fp)) {
      LOG_FATAL("Failed to compile ta shader.");
    }
  }

  if (!r_compile_program(r, &r->ui_program, NULL, ui_vp, ui_fp)) {
    LOG_FATAL("Failed to compile ui shader.");
  }
}

static void r_destroy_vertex_buffers(struct render_backend *r) {
  if (!r->ctx) {
    return;
  }

  glDeleteBuffers(1, &r->ui_ibo);
  glDeleteBuffers(1, &r->ui_vbo);
  glDeleteVertexArrays(1, &r->ui_vao);

  glDeleteBuffers(1, &r->ta_vbo);
  glDeleteVertexArrays(1, &r->ta_vao);
}

static void r_create_vertex_buffers(struct render_backend *r) {
  /* ui vao */
  {
    glGenVertexArrays(1, &r->ui_vao);
    glBindVertexArray(r->ui_vao);

    glGenBuffers(1, &r->ui_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->ui_vbo);

    glGenBuffers(1, &r->ui_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ui_ibo);

    /* xy */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex2),
                          (void *)offsetof(struct vertex2, xy));

    /* texcoord */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex2),
                          (void *)offsetof(struct vertex2, uv));

    /* color */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(struct vertex2),
                          (void *)offsetof(struct vertex2, color));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  /* ta vao */
  {
    glGenVertexArrays(1, &r->ta_vao);
    glBindVertexArray(r->ta_vao);

    glGenBuffers(1, &r->ta_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->ta_vbo);

    /* xyz */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                          (void *)offsetof(struct vertex, xyz));

    /* texcoord */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex),
                          (void *)offsetof(struct vertex, uv));

    /* color */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(struct vertex),
                          (void *)offsetof(struct vertex, color));

    /* offset color */
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(struct vertex),
                          (void *)offsetof(struct vertex, offset_color));

    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }
}

static void r_set_initial_state(struct render_backend *r) {
  r_set_depth_mask(r, 1);
  r_set_depth_func(r, DEPTH_NONE);
  r_set_cull_face(r, CULL_BACK);
  r_set_blend_func(r, BLEND_NONE, BLEND_NONE);
}

static struct shader_program *r_get_ta_program(struct render_backend *r,
                                               const struct surface *surf) {
  int idx = surf->shade;
  if (surf->texture) {
    idx |= ATTR_TEXTURE;
  }
  if (surf->ignore_alpha) {
    idx |= ATTR_IGNORE_ALPHA;
  }
  if (surf->ignore_texture_alpha) {
    idx |= ATTR_IGNORE_TEXTURE_ALPHA;
  }
  if (surf->offset_color) {
    idx |= ATTR_OFFSET_COLOR;
  }
  if (surf->pt_alpha_test) {
    idx |= ATTR_PT_ALPHA_TEST;
  }
  struct shader_program *program = &r->ta_programs[idx];
  CHECK_NOTNULL(program);
  return program;
}

void r_end_surfaces(struct render_backend *r) {}

void r_draw_surface(struct render_backend *r, const struct surface *surf) {
  r_set_depth_mask(r, surf->depth_write);
  r_set_depth_func(r, surf->depth_func);
  r_set_cull_face(r, surf->cull);
  r_set_blend_func(r, surf->src_blend, surf->dst_blend);

  struct shader_program *program = r_get_ta_program(r, surf);
  r_bind_program(r, program);

  /* if uniforms have yet to be bound for this program, do so now */
  if (program->uniform_token != r->uniform_token) {
    glUniformMatrix4fv(r_get_uniform(r, UNIFORM_MVP), 1, GL_FALSE,
                       r->uniform_mvp);
    glUniform1f(r_get_uniform(r, UNIFORM_PT_ALPHA_REF), surf->pt_alpha_ref);
    program->uniform_token = r->uniform_token;
  }

  if (surf->texture) {
    struct texture *tex = &r->textures[surf->texture];
    r_bind_texture(r, MAP_DIFFUSE, tex->texture);
  }

  glDrawArrays(GL_TRIANGLE_STRIP, surf->first_vert, surf->num_verts);
}

void r_begin_surfaces(struct render_backend *r, const float *projection,
                      const struct vertex *verts, int num_verts) {
  /* uniforms will be lazily bound for each program inside of r_draw_surface */
  r->uniform_token++;
  r->uniform_mvp = projection;

  glBindBuffer(GL_ARRAY_BUFFER, r->ta_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(struct vertex) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  r_bind_vao(r, r->ta_vao);
}

void r_end_surfaces2(struct render_backend *r) {}

void r_draw_surface2(struct render_backend *r, const struct surface2 *surf) {
  if (surf->scissor) {
    r_set_scissor_test(r, 1);
    r_set_scissor_clip(r, (int)surf->scissor_rect[0],
                       (int)surf->scissor_rect[1], (int)surf->scissor_rect[2],
                       (int)surf->scissor_rect[3]);
  } else {
    r_set_scissor_test(r, 0);
  }

  r_set_blend_func(r, surf->src_blend, surf->dst_blend);

  if (surf->framebuffer) {
    struct framebuffer *fb = &r->framebuffers[surf->framebuffer];
    r_bind_texture(r, MAP_DIFFUSE, fb->color_component);
  } else if (surf->texture) {
    struct texture *tex = &r->textures[surf->texture];
    r_bind_texture(r, MAP_DIFFUSE, tex->texture);
  } else {
    r_bind_texture(r, MAP_DIFFUSE, r->white.texture);
  }

  if (r->ui_use_ibo) {
    glDrawElements(prim_types[surf->prim_type], surf->num_verts,
                   GL_UNSIGNED_SHORT,
                   (void *)(intptr_t)(sizeof(uint16_t) * surf->first_vert));
  } else {
    glDrawArrays(prim_types[surf->prim_type], surf->first_vert,
                 surf->num_verts);
  }
}

void r_begin_surfaces2(struct render_backend *r, const struct vertex2 *verts,
                       int num_verts, uint16_t *indices, int num_indices) {
  glBindBuffer(GL_ARRAY_BUFFER, r->ui_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(struct vertex2) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  if (indices) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ui_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * num_indices,
                 indices, GL_DYNAMIC_DRAW);
    r->ui_use_ibo = 1;
  } else {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, -1);
    r->ui_use_ibo = 0;
  }
}

void r_end_ortho(struct render_backend *r) {
  r_set_scissor_test(r, 0);
}

void r_begin_ortho(struct render_backend *r) {
  float ortho[16];

  ortho[0] = 2.0f / (float)r->window->width;
  ortho[4] = 0.0f;
  ortho[8] = 0.0f;
  ortho[12] = -1.0f;

  ortho[1] = 0.0f;
  ortho[5] = -2.0f / (float)r->window->height;
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

  r_set_depth_mask(r, 0);
  r_set_depth_func(r, DEPTH_NONE);
  r_set_cull_face(r, CULL_NONE);

  r_bind_vao(r, r->ui_vao);
  r_bind_program(r, &r->ui_program);
  glUniformMatrix4fv(r_get_uniform(r, UNIFORM_MVP), 1, GL_FALSE, ortho);
}

void r_end_frame(struct render_backend *r) {
  SDL_GL_SwapWindow(r->window->handle);
}

void r_begin_frame(struct render_backend *r) {
  r_set_depth_mask(r, 1);

  glViewport(0, 0, r->window->width, r->window->height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void r_wait(sync_handle_t on) {
  GLsync sync = on;
  CHECK(glIsSync(sync));

  GLenum res = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, UINT64_MAX);
  CHECK(res == GL_ALREADY_SIGNALED || res == GL_CONDITION_SATISFIED);
  glDeleteSync(sync);
}

sync_handle_t r_sync(struct render_backend *r) {
  GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush();
  return sync;
}

void r_destroy_texture(struct render_backend *r, texture_handle_t handle) {
  struct texture *tex = &r->textures[handle];
  glDeleteTextures(1, &tex->texture);
  tex->texture = 0;
}

texture_handle_t r_create_texture(struct render_backend *r,
                                  enum pxl_format format,
                                  enum filter_mode filter,
                                  enum wrap_mode wrap_u, enum wrap_mode wrap_v,
                                  int mipmaps, int width, int height,
                                  const uint8_t *buffer) {
  /* find next open texture handle */
  texture_handle_t handle;
  for (handle = 1; handle < MAX_TEXTURES; handle++) {
    struct texture *tex = &r->textures[handle];
    if (!tex->texture) {
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

  struct texture *tex = &r->textures[handle];
  glGenTextures(1, &tex->texture);
  glBindTexture(GL_TEXTURE_2D, tex->texture);
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

void r_destroy_framebuffer(struct render_backend *r,
                           framebuffer_handle_t handle) {
  struct framebuffer *fb = &r->framebuffers[handle];

  glDeleteTextures(1, &fb->color_component);
  fb->color_component = 0;

  glDeleteRenderbuffers(1, &fb->depth_component);
  fb->depth_component = 0;

  glDeleteFramebuffers(1, &fb->fbo);
  fb->fbo = 0;
}

void r_bind_framebuffer(struct render_backend *r, framebuffer_handle_t handle) {
  struct framebuffer *fb = &r->framebuffers[handle];

  glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
}

framebuffer_handle_t r_create_framebuffer(struct render_backend *r) {
  /* find next open framebuffer handle */
  framebuffer_handle_t handle;
  for (handle = 1; handle < MAX_FRAMEBUFFERS; handle++) {
    struct framebuffer *fb = &r->framebuffers[handle];
    if (!fb->fbo) {
      break;
    }
  }
  CHECK_LT(handle, MAX_FRAMEBUFFERS);

  struct framebuffer *fb = &r->framebuffers[handle];

  /* create color component */
  glGenTextures(1, &fb->color_component);
  glBindTexture(GL_TEXTURE_2D, fb->color_component);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, r->window->width, r->window->height,
               0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);

  /* create depth component */
  glGenRenderbuffers(1, &fb->depth_component);
  glBindRenderbuffer(GL_RENDERBUFFER, fb->depth_component);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, r->window->width,
                        r->window->height);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  /* create fbo */
  glGenFramebuffers(1, &fb->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         fb->color_component, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, fb->depth_component);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  CHECK_EQ(status, GL_FRAMEBUFFER_COMPLETE);

  /* switch back to default framebuffer */
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  return handle;
}

void r_destroy(struct render_backend *r) {
  r_destroy_vertex_buffers(r);
  r_destroy_shaders(r);
  r_destroy_textures(r);

  win_gl_destroy_context(r->window, r->ctx);

  free(r);
}

struct render_backend *r_create(struct window *window) {
  struct render_backend *r = calloc(1, sizeof(struct render_backend));

  r->window = window;

  /* setup gl context */
  r->ctx = win_gl_create_context(r->window);

  r_create_textures(r);
  r_create_shaders(r);
  r_create_vertex_buffers(r);
  r_set_initial_state(r);

  return r;
}
