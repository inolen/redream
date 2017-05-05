#include <GL/glew.h>
#include "core/assert.h"
#include "core/profiler.h"
#include "core/string.h"
#include "sys/thread.h"
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
  GLuint prog;
  GLuint vertex_shader;
  GLuint fragment_shader;
  GLint loc[UNIFORM_NUM_UNIFORMS];
  uint64_t uniform_token;
};

struct framebuffer {
  GLuint fbo;
  GLuint color_texture;
  GLuint depth_buffer;
};

struct texture {
  GLuint texture;
};

struct render_backend {
  struct window *win;
  glcontext_t ctx;

  /* default assets created during intitialization */
  GLuint white_texture;
  struct shader_program ta_programs[ATTR_COUNT];
  struct shader_program ui_program;

  /* note, in this backend framebuffer_handle_t and texture_handle_t are the
     OpenGL object handles, not indexes into these arrays. this lets OpenGL
     handle generating unique IDs across multiple contexts, with no additional
     synchronization on our part. however, to delete an object a reverse lookup
     must be performed to match the handle to an index in these arrays

     note note, due to this dumbed down design, textures can be shared across
     multiple backends for rendering purposes, but can only be deleted on the
     backend that created them

     TODO the textures / framebuffers arrays exist purely for cleanup purposes,
     it'd be nice to replace with a hashtable to avoid O(n) reverse lookup */
  struct texture textures[MAX_TEXTURES];
  struct framebuffer framebuffers[MAX_FRAMEBUFFERS];

  /* surface render state */
  GLuint ta_vao;
  GLuint ta_vbo;
  GLuint ui_vao;
  GLuint ui_vbo;
  GLuint ui_ibo;
  int ui_use_ibo;

  /* current gl state */
  uint64_t uniform_token;
  const float *uniform_mvp;
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

static inline void r_bind_texture(struct render_backend *r,
                                  enum texture_map map, GLuint tex) {
  glActiveTexture(GL_TEXTURE0 + map);
  glBindTexture(GL_TEXTURE_2D, tex);
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

  glDeleteProgram(program->prog);
}

static int r_compile_program(struct render_backend *r,
                             struct shader_program *program, const char *header,
                             const char *vertex_source,
                             const char *fragment_source) {
  char buffer[16384] = {0};

  memset(program, 0, sizeof(*program));
  program->prog = glCreateProgram();

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

    glAttachShader(program->prog, program->vertex_shader);
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

    glAttachShader(program->prog, program->fragment_shader);
  }

  glLinkProgram(program->prog);

  GLint linked;
  glGetProgramiv(program->prog, GL_LINK_STATUS, &linked);

  if (!linked) {
    r_destroy_program(program);
    return 0;
  }

  for (int i = 0; i < UNIFORM_NUM_UNIFORMS; i++) {
    program->loc[i] = glGetUniformLocation(program->prog, uniform_names[i]);
  }

  /* bind diffuse sampler once after compile, this currently never changes */
  glUseProgram(program->prog);
  glUniform1i(program->loc[UNIFORM_DIFFUSE], MAP_DIFFUSE);
  glUseProgram(0);

  return 1;
}

static void r_destroy_shaders(struct render_backend *r) {
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

static void r_destroy_textures(struct render_backend *r) {
  glDeleteTextures(1, &r->white_texture);

  for (int i = 0; i < MAX_TEXTURES; i++) {
    struct texture *tex = &r->textures[i];

    if (!tex->texture) {
      continue;
    }

    glDeleteTextures(1, &tex->texture);
  }
}

static void r_create_textures(struct render_backend *r) {
  /* create default all white texture */
  uint8_t pixels[64 * 64 * 4];
  memset(pixels, 0xff, sizeof(pixels));

  glGenTextures(1, &r->white_texture);
  glBindTexture(GL_TEXTURE_2D, r->white_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
}

static void r_destroy_vertex_arrays(struct render_backend *r) {
  glDeleteBuffers(1, &r->ui_ibo);
  glDeleteBuffers(1, &r->ui_vbo);
  glDeleteVertexArrays(1, &r->ui_vao);

  glDeleteBuffers(1, &r->ta_vbo);
  glDeleteVertexArrays(1, &r->ta_vao);
}

static void r_create_vertex_arrays(struct render_backend *r) {
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
  glDepthMask(1);
  glDisable(GL_DEPTH_TEST);

  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);

  glDisable(GL_BLEND);
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
  glDepthMask(!!surf->depth_write);

  if (surf->depth_func == DEPTH_NONE) {
    glDisable(GL_DEPTH_TEST);
  } else {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(depth_funcs[surf->depth_func]);
  }

  if (surf->cull == CULL_NONE) {
    glDisable(GL_CULL_FACE);
  } else {
    glEnable(GL_CULL_FACE);
    glCullFace(cull_face[surf->cull]);
  }

  if (surf->src_blend == BLEND_NONE || surf->dst_blend == BLEND_NONE) {
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendFunc(blend_funcs[surf->src_blend], blend_funcs[surf->dst_blend]);
  }

  struct shader_program *program = r_get_ta_program(r, surf);

  glUseProgram(program->prog);

  /* if uniforms have yet to be bound for this program, do so now */
  if (program->uniform_token != r->uniform_token) {
    glUniformMatrix4fv(program->loc[UNIFORM_MVP], 1, GL_FALSE, r->uniform_mvp);
    glUniform1f(program->loc[UNIFORM_PT_ALPHA_REF], surf->pt_alpha_ref);
    program->uniform_token = r->uniform_token;
  }

  if (surf->texture) {
    r_bind_texture(r, MAP_DIFFUSE, surf->texture);
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
  glBindVertexArray(r->ta_vao);
}

void r_end_surfaces2(struct render_backend *r) {}

void r_draw_surface2(struct render_backend *r, const struct surface2 *surf) {
  if (surf->scissor) {
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)surf->scissor_rect[0], (int)surf->scissor_rect[1],
              (int)surf->scissor_rect[2], (int)surf->scissor_rect[3]);
  } else {
    glDisable(GL_SCISSOR_TEST);
  }

  if (surf->src_blend == BLEND_NONE || surf->dst_blend == BLEND_NONE) {
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendFunc(blend_funcs[surf->src_blend], blend_funcs[surf->dst_blend]);
  }

  if (surf->texture) {
    r_bind_texture(r, MAP_DIFFUSE, surf->texture);
  } else {
    r_bind_texture(r, MAP_DIFFUSE, r->white_texture);
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
  glDisable(GL_SCISSOR_TEST);
}

void r_begin_ortho(struct render_backend *r) {
  int width = win_width(r->win);
  int height = win_height(r->win);

  float ortho[16];
  ortho[0] = 2.0f / (float)width;
  ortho[4] = 0.0f;
  ortho[8] = 0.0f;
  ortho[12] = -1.0f;

  ortho[1] = 0.0f;
  ortho[5] = -2.0f / (float)height;
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

  glDepthMask(0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  struct shader_program *program = &r->ui_program;
  glBindVertexArray(r->ui_vao);
  glUseProgram(program->prog);
  glUniformMatrix4fv(program->loc[UNIFORM_MVP], 1, GL_FALSE, ortho);
}

void r_swap_buffers(struct render_backend *r) {
  win_gl_swap_buffers(r->win);
}

void r_clear_viewport(struct render_backend *r) {
  int width = win_width(r->win);
  int height = win_height(r->win);

  glDepthMask(1);

  glViewport(0, 0, width, height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void r_destroy_sync(struct render_backend *r, sync_handle_t handle) {
  GLsync sync = handle;
  DCHECK(glIsSync(sync));

  glDeleteSync(sync);
}

void r_wait_sync(struct render_backend *r, sync_handle_t handle) {
  GLsync sync = handle;
  DCHECK(glIsSync(sync));

  glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
}

sync_handle_t r_insert_sync(struct render_backend *r) {
  GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush();
  return sync;
}

void r_destroy_texture(struct render_backend *r, texture_handle_t handle) {
  /* lookup texture entry
     FIXME need common hashtable */
  int entry;
  for (entry = 0; entry < MAX_TEXTURES; entry++) {
    struct texture *tex = &r->textures[entry];
    if (tex->texture == handle) {
      break;
    }
  }
  CHECK_LT(entry, MAX_TEXTURES);

  struct texture *tex = &r->textures[entry];
  glDeleteTextures(1, &tex->texture);
  tex->texture = 0;
}

texture_handle_t r_create_texture(struct render_backend *r,
                                  enum pxl_format format,
                                  enum filter_mode filter,
                                  enum wrap_mode wrap_u, enum wrap_mode wrap_v,
                                  int mipmaps, int width, int height,
                                  const uint8_t *buffer) {
  /* find next open texture entry */
  int entry;
  for (entry = 0; entry < MAX_TEXTURES; entry++) {
    struct texture *tex = &r->textures[entry];
    if (!tex->texture) {
      break;
    }
  }
  CHECK_LT(entry, MAX_TEXTURES);

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

  struct texture *tex = &r->textures[entry];
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

  return tex->texture;
}

void r_destroy_framebuffer(struct render_backend *r,
                           framebuffer_handle_t handle) {
  /* lookup framebuffer entry */
  int entry;
  for (entry = 0; entry < MAX_FRAMEBUFFERS; entry++) {
    struct framebuffer *fb = &r->framebuffers[entry];
    if (fb->fbo == handle) {
      break;
    }
  }
  CHECK_LT(entry, MAX_FRAMEBUFFERS);

  struct framebuffer *fb = &r->framebuffers[entry];

  glDeleteTextures(1, &fb->color_texture);
  fb->color_texture = 0;

  glDeleteRenderbuffers(1, &fb->depth_buffer);
  fb->depth_buffer = 0;

  glDeleteFramebuffers(1, &fb->fbo);
  fb->fbo = 0;
}

void r_bind_framebuffer(struct render_backend *r, framebuffer_handle_t handle) {
  glBindFramebuffer(GL_FRAMEBUFFER, handle);
}

framebuffer_handle_t r_create_framebuffer(struct render_backend *r,
                                          texture_handle_t *color_texture) {
  int width = win_width(r->win);
  int height = win_height(r->win);

  /* find next open framebuffer handle */
  int entry;
  for (entry = 0; entry < MAX_FRAMEBUFFERS; entry++) {
    struct framebuffer *fb = &r->framebuffers[entry];
    if (!fb->fbo) {
      break;
    }
  }
  CHECK_LT(entry, MAX_FRAMEBUFFERS);

  struct framebuffer *fb = &r->framebuffers[entry];

  /* create color component */
  glGenTextures(1, &fb->color_texture);
  glBindTexture(GL_TEXTURE_2D, fb->color_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);

  /* create depth component */
  glGenRenderbuffers(1, &fb->depth_buffer);
  glBindRenderbuffer(GL_RENDERBUFFER, fb->depth_buffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, width, height);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  /* create fbo */
  glGenFramebuffers(1, &fb->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         fb->color_texture, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, fb->depth_buffer);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  CHECK_EQ(status, GL_FRAMEBUFFER_COMPLETE);

  /* switch back to default framebuffer */
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  *color_texture = fb->color_texture;

  return fb->fbo;
}

int r_video_height(struct render_backend *r) {
  return win_height(r->win);
}

int r_video_width(struct render_backend *r) {
  return win_width(r->win);
}

static void r_check_one_per_thread() {
  /* to keep things simple, don't allow more than one gl backend per thread.
     this avoids providing interfaces to manage the current gl context */
  static _Thread_local int initialized;
  CHECK_EQ(initialized, 0);
  initialized = 1;
}

void r_destroy(struct render_backend *r) {
  r_destroy_vertex_arrays(r);
  r_destroy_shaders(r);
  r_destroy_textures(r);

  win_gl_destroy_context(r->win, r->ctx);

  free(r);
}

struct render_backend *r_create_from(struct render_backend *from) {
  r_check_one_per_thread();

  struct render_backend *r = calloc(1, sizeof(struct render_backend));

  r->win = from->win;
  r->ctx = win_gl_create_context_from(r->win, from->ctx);

  r_create_textures(r);
  r_create_shaders(r);
  r_create_vertex_arrays(r);

  r_set_initial_state(r);

  return r;
}

struct render_backend *r_create(struct window *win) {
  r_check_one_per_thread();

  struct render_backend *r = calloc(1, sizeof(struct render_backend));

  r->win = win;
  r->ctx = win_gl_create_context(win);

  r_create_textures(r);
  r_create_shaders(r);
  r_create_vertex_arrays(r);

  r_set_initial_state(r);

  return r;
}
