#include <glad/glad.h>
#include "core/core.h"
#include "host/host.h"
#include "render/render_backend.h"

enum texture_map {
  MAP_DIFFUSE,
};

enum uniform_attr {
  UNIFORM_PROJ,
  UNIFORM_DIFFUSE,
  UNIFORM_VIDEO_SCALE,
  UNIFORM_ALPHA_REF,
  UNIFORM_NUM_UNIFORMS,
};

static const char *uniform_names[] = {
    "u_proj", "u_diffuse", "u_video_scale", "u_alpha_ref",
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
  ATTR_ALPHA_TEST = 0x40,
  ATTR_DEBUG_DEPTH_BUFFER = 0x80,
  ATTR_COUNT = 0x100
};

struct shader_program {
  GLuint prog;
  GLuint vertex_shader;
  GLuint fragment_shader;
  GLint loc[UNIFORM_NUM_UNIFORMS];

  /* the last global uniforms bound to this program */
  uint64_t uniform_token;
};

struct texture {
  GLuint texture;
};

struct viewport {
  int x, y, w, h;
};

struct render_backend {
  struct host *host;
  int width, height;

  /* current viewport */
  struct viewport viewport;

  /* default assets created during intitialization */
  GLuint white_texture;
  struct shader_program ta_programs[ATTR_COUNT];
  struct shader_program ui_program;

  /* offscreen framebuffer for blitting raw pixels */
  GLuint pixel_fbo;
  GLuint pixel_texture;

  /* texture cache */
  struct texture textures[MAX_TEXTURES];

  /* surface render state */
  GLuint ta_vao;
  GLuint ta_vbo;
  GLuint ta_ibo;
  GLuint ui_vao;
  GLuint ui_vbo;
  GLuint ui_ibo;
  int ui_use_ibo;

  /* global uniforms that are constant for every surface rendered between a call
     to begin_surfaces and end_surfaces */
  uint64_t uniform_token;
  float uniform_video_scale[4];
};

#include "render/ta.glsl"
#include "render/ui.glsl"

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

static GLuint internal_formats[] = {
    GL_RGB,  /* PXL_RGB */
    GL_RGBA, /* PXL_RGBA */
    GL_RGBA, /* PXL_RGBA5551 */
    GL_RGB,  /* PXL_RGB565 */
    GL_RGBA, /* PXL_RGBA4444 */
};

static GLuint pixel_formats[] = {
    GL_UNSIGNED_BYTE,          /* PXL_RGB */
    GL_UNSIGNED_BYTE,          /* PXL_RGBA */
    GL_UNSIGNED_SHORT_5_5_5_1, /* PXL_RGBA5551 */
    GL_UNSIGNED_SHORT_5_6_5,   /* PXL_RGB565 */
    GL_UNSIGNED_SHORT_4_4_4_4, /* PXL_RGBA4444 */
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
  if (program->vertex_shader) {
    glDeleteShader(program->vertex_shader);
  }

  if (program->fragment_shader) {
    glDeleteShader(program->fragment_shader);
  }

  if (program->prog) {
    glDeleteProgram(program->prog);
  }
}

static int r_compile_program(struct render_backend *r,
                             struct shader_program *program, const char *header,
                             const char *vertex_source,
                             const char *fragment_source) {
  char buffer[16384] = {0};

#if PLATFORM_ANDROID
#define GLSL_VERSION "310 es"
#else
#define GLSL_VERSION "330 core"
#endif

  memset(program, 0, sizeof(*program));
  program->prog = glCreateProgram();

  if (vertex_source) {
    snprintf(buffer, sizeof(buffer) - 1, "#version " GLSL_VERSION
                                         "\n"
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
    snprintf(buffer, sizeof(buffer) - 1, "#version " GLSL_VERSION
                                         "\n"
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
  /* ta shaders are lazy-compiled in r_get_ta_program to improve startup time */

  if (!r_compile_program(r, &r->ui_program, NULL, ui_vp, ui_fp)) {
    LOG_FATAL("failed to compile ui shader");
  }
}

static void r_destroy_textures(struct render_backend *r) {
  glDeleteTextures(1, &r->white_texture);

  glDeleteFramebuffers(1, &r->pixel_fbo);
  glDeleteTextures(1, &r->pixel_texture);

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

  /* create fbo for blitting raw framebuffers to */
  glGenFramebuffers(1, &r->pixel_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, r->pixel_fbo);

  glGenTextures(1, &r->pixel_texture);
  glBindTexture(GL_TEXTURE_2D, r->pixel_texture);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB,
               GL_UNSIGNED_SHORT_5_6_5, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  GLenum buffers[] = {GL_COLOR_ATTACHMENT0};
  glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, r->pixel_texture,
                       0);
  glDrawBuffers(ARRAY_SIZE(buffers), buffers);

  GLenum res = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  CHECK_EQ(res, GL_FRAMEBUFFER_COMPLETE);

  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void r_destroy_vertex_arrays(struct render_backend *r) {
  glDeleteBuffers(1, &r->ui_ibo);
  glDeleteBuffers(1, &r->ui_vbo);
  glDeleteVertexArrays(1, &r->ui_vao);

  glDeleteBuffers(1, &r->ta_ibo);
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
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct ui_vertex),
                          (void *)offsetof(struct ui_vertex, xy));

    /* texcoord */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct ui_vertex),
                          (void *)offsetof(struct ui_vertex, uv));

    /* color */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(struct ui_vertex),
                          (void *)offsetof(struct ui_vertex, color));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  /* ta vao */
  {
    glGenVertexArrays(1, &r->ta_vao);
    glBindVertexArray(r->ta_vao);

    glGenBuffers(1, &r->ta_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->ta_vbo);

    glGenBuffers(1, &r->ta_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ta_ibo);

    /* xyz */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct ta_vertex),
                          (void *)offsetof(struct ta_vertex, xyz));

    /* texcoord */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct ta_vertex),
                          (void *)offsetof(struct ta_vertex, uv));

    /* color */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(struct ta_vertex),
                          (void *)offsetof(struct ta_vertex, color));

    /* offset color */
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(struct ta_vertex),
                          (void *)offsetof(struct ta_vertex, offset_color));

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
                                               const struct ta_surface *surf) {
  int idx = (int)surf->params.shade;
  if (surf->params.texture) {
    idx |= ATTR_TEXTURE;
  }
  if (surf->params.ignore_alpha) {
    idx |= ATTR_IGNORE_ALPHA;
  }
  if (surf->params.ignore_texture_alpha) {
    idx |= ATTR_IGNORE_TEXTURE_ALPHA;
  }
  if (surf->params.offset_color) {
    idx |= ATTR_OFFSET_COLOR;
  }
  if (surf->params.alpha_test) {
    idx |= ATTR_ALPHA_TEST;
  }
  if (surf->params.debug_depth) {
    idx |= ATTR_DEBUG_DEPTH_BUFFER;
  }

  struct shader_program *program = &r->ta_programs[idx];

  /* lazy-compile the ta programs */
  if (!program->prog) {
    char header[1024];

    header[0] = 0;

    if ((idx & ATTR_SHADE_MASK) == ATTR_SHADE_DECAL) {
      strcat(header, "#define SHADE_DECAL\n");
    } else if ((idx & ATTR_SHADE_MASK) == ATTR_SHADE_MODULATE) {
      strcat(header, "#define SHADE_MODULATE\n");
    } else if ((idx & ATTR_SHADE_MASK) == ATTR_SHADE_DECAL_ALPHA) {
      strcat(header, "#define SHADE_DECAL_ALPHA\n");
    } else if ((idx & ATTR_SHADE_MASK) == ATTR_SHADE_MODULATE_ALPHA) {
      strcat(header, "#define SHADE_MODULATE_ALPHA\n");
    }

    if (idx & ATTR_TEXTURE) {
      strcat(header, "#define TEXTURE\n");
    }
    if (idx & ATTR_IGNORE_ALPHA) {
      strcat(header, "#define IGNORE_ALPHA\n");
    }
    if (idx & ATTR_IGNORE_TEXTURE_ALPHA) {
      strcat(header, "#define IGNORE_TEXTURE_ALPHA\n");
    }
    if (idx & ATTR_OFFSET_COLOR) {
      strcat(header, "#define OFFSET_COLOR\n");
    }
    if (idx & ATTR_ALPHA_TEST) {
      strcat(header, "#define ALPHA_TEST\n");
    }
    if (idx & ATTR_DEBUG_DEPTH_BUFFER) {
      strcat(header, "#define DEBUG_DEPTH_BUFFER\n");
    }

    int res = r_compile_program(r, program, header, ta_vp, ta_fp);
    CHECK(res, "failed to compile ta shader");
  }

  return program;
}

void r_end_ui_surfaces(struct render_backend *r) {
  glDisable(GL_SCISSOR_TEST);
}

void r_draw_ui_surface(struct render_backend *r,
                       const struct ui_surface *surf) {
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
    struct texture *tex = &r->textures[surf->texture];
    r_bind_texture(r, MAP_DIFFUSE, tex->texture);
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

void r_begin_ui_surfaces(struct render_backend *r,
                         const struct ui_vertex *verts, int num_verts,
                         const uint16_t *indices, int num_indices) {
  /* setup projection matrix */
  float ortho[16];
  ortho[0] = 2.0f / (float)r->viewport.w;
  ortho[4] = 0.0f;
  ortho[8] = 0.0f;
  ortho[12] = -1.0f;

  ortho[1] = 0.0f;
  ortho[5] = -2.0f / (float)r->viewport.h;
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
  glUniformMatrix4fv(program->loc[UNIFORM_PROJ], 1, GL_FALSE, ortho);

  /* bind buffers */
  glBindBuffer(GL_ARRAY_BUFFER, r->ui_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(struct ui_vertex) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  if (indices) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ui_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * num_indices,
                 indices, GL_DYNAMIC_DRAW);
    r->ui_use_ibo = 1;
  } else {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    r->ui_use_ibo = 0;
  }
}

void r_end_ta_surfaces(struct render_backend *r) {}

void r_draw_ta_surface(struct render_backend *r,
                       const struct ta_surface *surf) {
  glDepthMask(!!surf->params.depth_write);

  if (surf->params.depth_func == DEPTH_NONE) {
    glDisable(GL_DEPTH_TEST);
  } else {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(depth_funcs[surf->params.depth_func]);
  }

  if (surf->params.cull == CULL_NONE) {
    glDisable(GL_CULL_FACE);
  } else {
    glEnable(GL_CULL_FACE);
    glCullFace(cull_face[surf->params.cull]);
  }

  if (surf->params.src_blend == BLEND_NONE ||
      surf->params.dst_blend == BLEND_NONE) {
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendFunc(blend_funcs[surf->params.src_blend],
                blend_funcs[surf->params.dst_blend]);
  }

  struct shader_program *program = r_get_ta_program(r, surf);

  glUseProgram(program->prog);

  /* bind global uniforms if they've changed */
  if (program->uniform_token != r->uniform_token) {
    glUniform4fv(program->loc[UNIFORM_VIDEO_SCALE], 1, r->uniform_video_scale);
    program->uniform_token = r->uniform_token;
  }

  /* bind non-global uniforms every time */
  float alpha_ref = surf->params.alpha_ref / 255.0f;
  glUniform1f(program->loc[UNIFORM_ALPHA_REF], alpha_ref);

  if (surf->params.texture) {
    struct texture *tex = &r->textures[surf->params.texture];
    r_bind_texture(r, MAP_DIFFUSE, tex->texture);
  }

  glDrawElements(GL_TRIANGLES, surf->num_verts, GL_UNSIGNED_SHORT,
                 (void *)(intptr_t)(sizeof(uint16_t) * surf->first_vert));
}

void r_begin_ta_surfaces(struct render_backend *r, int video_width,
                         int video_height, const struct ta_vertex *verts,
                         int num_verts, const uint16_t *indices,
                         int num_indices) {
  /* uniforms will be lazily bound for each program inside of r_draw_surface */
  r->uniform_token++;
  r->uniform_video_scale[0] = 2.0f / (float)video_width;
  r->uniform_video_scale[1] = -1.0f;
  r->uniform_video_scale[2] = -2.0f / (float)video_height;
  r->uniform_video_scale[3] = 1.0f;

  glBindVertexArray(r->ta_vao);

  glBindBuffer(GL_ARRAY_BUFFER, r->ta_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(struct ta_vertex) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, r->ta_ibo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * num_indices, indices,
               GL_DYNAMIC_DRAW);
}

void r_draw_pixels(struct render_backend *r, const uint8_t *pixels, int x,
                   int y, int width, int height) {
  glBindTexture(GL_TEXTURE_2D, r->pixel_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
               GL_UNSIGNED_BYTE, pixels);
  glBindTexture(GL_TEXTURE_2D, 0);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, r->pixel_fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glBlitFramebuffer(x, y + height, x + width, y, r->viewport.x, r->viewport.y,
                    r->viewport.x + r->viewport.w,
                    r->viewport.y + r->viewport.h, GL_COLOR_BUFFER_BIT,
                    GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void r_viewport(struct render_backend *r, int x, int y, int width, int height) {
  r->viewport.x = x;
  r->viewport.y = y;
  r->viewport.w = width;
  r->viewport.h = height;
  glViewport(r->viewport.x, r->viewport.y, r->viewport.w, r->viewport.h);
}

void r_clear(struct render_backend *r) {
  glDepthMask(1);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
}

void r_destroy_texture(struct render_backend *r, texture_handle_t handle) {
  if (!handle) {
    return;
  }

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
  /* find next open texture entry */
  texture_handle_t handle;
  for (handle = 1; handle < MAX_TEXTURES; handle++) {
    struct texture *tex = &r->textures[handle];
    if (!tex->texture) {
      break;
    }
  }
  CHECK_LT(handle, MAX_TEXTURES);

  GLuint internal_fmt = internal_formats[format];
  GLuint pixel_fmt = pixel_formats[format];

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

int r_height(struct render_backend *r) {
  return r->height;
}

int r_width(struct render_backend *r) {
  return r->width;
}

void r_destroy(struct render_backend *r) {
  r_destroy_vertex_arrays(r);
  r_destroy_shaders(r);
  r_destroy_textures(r);

  free(r);
}

struct render_backend *r_create(int width, int height) {
  struct render_backend *r = calloc(1, sizeof(struct render_backend));

  r->width = width;
  r->height = height;

  r_create_textures(r);
  r_create_shaders(r);
  r_create_vertex_arrays(r);
  r_set_initial_state(r);

  return r;
}
