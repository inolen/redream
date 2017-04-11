#include <GL/glew.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include "core/assert.h"
#include "core/profiler.h"
#include "core/string.h"
#include "ui/nuklear.h"
#include "ui/window.h"
#include "video/render_backend.h"

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

struct render_backend {
  struct window *window;

  glcontext_t ctx;
  int debug_wireframe;

  /* resources */
  GLuint textures[MAX_TEXTURES];
  GLuint white_tex;

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

static void rb_set_scissor_test(struct render_backend *rb, int enabled) {
  if (rb->scissor_test == enabled) {
    return;
  }

  rb->scissor_test = enabled;

  if (enabled) {
    glEnable(GL_SCISSOR_TEST);
  } else {
    glDisable(GL_SCISSOR_TEST);
  }
}

static void rb_set_scissor_clip(struct render_backend *rb, int x, int y,
                                int width, int height) {
  glScissor(x, y, width, height);
}

static void rb_set_depth_mask(struct render_backend *rb, int enabled) {
  if (rb->depth_mask == enabled) {
    return;
  }

  rb->depth_mask = enabled;

  glDepthMask(enabled ? 1 : 0);
}

static void rb_set_depth_func(struct render_backend *rb, enum depth_func fn) {
  if (rb->depth_func == fn) {
    return;
  }

  rb->depth_func = fn;

  if (fn == DEPTH_NONE) {
    glDisable(GL_DEPTH_TEST);
  } else {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(depth_funcs[fn]);
  }
}

static void rb_set_cull_face(struct render_backend *rb, enum cull_face fn) {
  if (rb->cull_face == fn) {
    return;
  }

  rb->cull_face = fn;

  if (fn == CULL_NONE) {
    glDisable(GL_CULL_FACE);
  } else {
    glEnable(GL_CULL_FACE);
    glCullFace(cull_face[fn]);
  }
}

static void rb_set_blend_func(struct render_backend *rb, enum blend_func src_fn,
                              enum blend_func dst_fn) {
  if (rb->src_blend == src_fn && rb->dst_blend == dst_fn) {
    return;
  }

  rb->src_blend = src_fn;
  rb->dst_blend = dst_fn;

  if (src_fn == BLEND_NONE || dst_fn == BLEND_NONE) {
    glDisable(GL_BLEND);
  } else {
    glEnable(GL_BLEND);
    glBlendFunc(blend_funcs[src_fn], blend_funcs[dst_fn]);
  }
}

static void rb_bind_vao(struct render_backend *rb, GLuint vao) {
  if (rb->current_vao == vao) {
    return;
  }

  rb->current_vao = vao;

  glBindVertexArray(vao);
}

static void rb_bind_program(struct render_backend *rb,
                            struct shader_program *program) {
  if (rb->current_program == program) {
    return;
  }

  rb->current_program = program;

  glUseProgram(program ? program->program : 0);
}

void rb_bind_texture(struct render_backend *rb, enum texture_map map,
                     GLuint tex) {
  glActiveTexture(GL_TEXTURE0 + map);
  glBindTexture(GL_TEXTURE_2D, tex);
}

static GLint rb_get_uniform(struct render_backend *rb, enum uniform_attr attr) {
  return rb->current_program->uniforms[attr];
}

static void rb_print_shader_log(GLuint shader) {
  int max_length, length;
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_length);

  char *info_log = malloc(max_length);
  glGetShaderInfoLog(shader, max_length, &length, info_log);
  LOG_INFO(info_log);
  free(info_log);
}

static int rb_compile_shader(const char *source, GLenum shader_type,
                             GLuint *shader) {
  size_t sourceLength = strlen(source);

  *shader = glCreateShader(shader_type);
  glShaderSource(*shader, 1, (const GLchar **)&source,
                 (const GLint *)&sourceLength);
  glCompileShader(*shader);

  GLint compiled;
  glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);

  if (!compiled) {
    rb_print_shader_log(*shader);
    glDeleteShader(*shader);
    return 0;
  }

  return 1;
}

static void rb_destroy_program(struct shader_program *program) {
  if (program->vertex_shader > 0) {
    glDeleteShader(program->vertex_shader);
  }

  if (program->fragment_shader > 0) {
    glDeleteShader(program->fragment_shader);
  }

  glDeleteProgram(program->program);
}

static int rb_compile_program(struct render_backend *rb,
                              struct shader_program *program,
                              const char *header, const char *vertex_source,
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

    if (!rb_compile_shader(buffer, GL_VERTEX_SHADER, &program->vertex_shader)) {
      rb_destroy_program(program);
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

    if (!rb_compile_shader(buffer, GL_FRAGMENT_SHADER,
                           &program->fragment_shader)) {
      rb_destroy_program(program);
      return 0;
    }

    glAttachShader(program->program, program->fragment_shader);
  }

  glLinkProgram(program->program);

  GLint linked;
  glGetProgramiv(program->program, GL_LINK_STATUS, &linked);

  if (!linked) {
    rb_destroy_program(program);
    return 0;
  }

  for (int i = 0; i < UNIFORM_NUM_UNIFORMS; i++) {
    program->uniforms[i] =
        glGetUniformLocation(program->program, uniform_names[i]);
  }

  /* bind diffuse sampler once after compile, this currently never changes */
  rb_bind_program(rb, program);
  glUniform1i(rb_get_uniform(rb, UNIFORM_DIFFUSE), MAP_DIFFUSE);
  rb_bind_program(rb, NULL);

  return 1;
}

static void rb_destroy_textures(struct render_backend *rb) {
  if (!rb->ctx) {
    return;
  }

  glDeleteTextures(1, &rb->white_tex);

  for (int i = 1; i < MAX_TEXTURES; i++) {
    if (!rb->textures[i]) {
      continue;
    }
    glDeleteTextures(1, &rb->textures[i]);
  }
}

static void rb_create_textures(struct render_backend *rb) {
  uint8_t pixels[64 * 64 * 4];

  memset(pixels, 0xff, sizeof(pixels));
  glGenTextures(1, &rb->white_tex);
  glBindTexture(GL_TEXTURE_2D, rb->white_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);
  glBindTexture(GL_TEXTURE_2D, 0);
}

static void rb_destroy_shaders(struct render_backend *rb) {
  if (!rb->ctx) {
    return;
  }

  for (int i = 0; i < ATTR_COUNT; i++) {
    rb_destroy_program(&rb->ta_programs[i]);
  }

  rb_destroy_program(&rb->ui_program);
}

static void rb_create_shaders(struct render_backend *rb) {
  char header[1024];

  for (int i = 0; i < ATTR_COUNT; i++) {
    struct shader_program *program = &rb->ta_programs[i];

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

    if (!rb_compile_program(rb, program, header, ta_vp, ta_fp)) {
      LOG_FATAL("Failed to compile ta shader.");
    }
  }

  if (!rb_compile_program(rb, &rb->ui_program, NULL, ui_vp, ui_fp)) {
    LOG_FATAL("Failed to compile ui shader.");
  }
}

static void rb_destroy_vertex_buffers(struct render_backend *rb) {
  if (!rb->ctx) {
    return;
  }

  glDeleteBuffers(1, &rb->ui_ibo);
  glDeleteBuffers(1, &rb->ui_vbo);
  glDeleteVertexArrays(1, &rb->ui_vao);

  glDeleteBuffers(1, &rb->ta_vbo);
  glDeleteVertexArrays(1, &rb->ta_vao);
}

static void rb_create_vertex_buffers(struct render_backend *rb) {
  /* ui vao */
  {
    glGenVertexArrays(1, &rb->ui_vao);
    glBindVertexArray(rb->ui_vao);

    glGenBuffers(1, &rb->ui_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, rb->ui_vbo);

    glGenBuffers(1, &rb->ui_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rb->ui_ibo);

    /* xy */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex2d),
                          (void *)offsetof(struct vertex2d, xy));

    /* texcoord */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex2d),
                          (void *)offsetof(struct vertex2d, uv));

    /* color */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(struct vertex2d),
                          (void *)offsetof(struct vertex2d, color));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

  /* ta vao */
  {
    glGenVertexArrays(1, &rb->ta_vao);
    glBindVertexArray(rb->ta_vao);

    glGenBuffers(1, &rb->ta_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, rb->ta_vbo);

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

static void rb_set_initial_state(struct render_backend *rb) {
  rb_set_depth_mask(rb, 1);
  rb_set_depth_func(rb, DEPTH_NONE);
  rb_set_cull_face(rb, CULL_BACK);
  rb_set_blend_func(rb, BLEND_NONE, BLEND_NONE);
}

static struct shader_program *rb_get_ta_program(struct render_backend *rb,
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
  struct shader_program *program = &rb->ta_programs[idx];
  CHECK_NOTNULL(program);
  return program;
}

void rb_end_surfaces(struct render_backend *rb) {
  if (rb->debug_wireframe) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  }
}

void rb_draw_surface(struct render_backend *rb, const struct surface *surf) {
  rb_set_depth_mask(rb, surf->depth_write);
  rb_set_depth_func(rb, surf->depth_func);
  rb_set_cull_face(rb, surf->cull);
  rb_set_blend_func(rb, surf->src_blend, surf->dst_blend);

  struct shader_program *program = rb_get_ta_program(rb, surf);
  rb_bind_program(rb, program);

  /* if uniforms have yet to be bound for this program, do so now */
  if (program->uniform_token != rb->uniform_token) {
    glUniformMatrix4fv(rb_get_uniform(rb, UNIFORM_MVP), 1, GL_FALSE,
                       rb->uniform_mvp);
    glUniform1f(rb_get_uniform(rb, UNIFORM_PT_ALPHA_REF), surf->pt_alpha_ref);
    program->uniform_token = rb->uniform_token;
  }

  if (surf->texture) {
    rb_bind_texture(rb, MAP_DIFFUSE, rb->textures[surf->texture]);
  }

  glDrawArrays(GL_TRIANGLE_STRIP, surf->first_vert, surf->num_verts);
}

void rb_begin_surfaces(struct render_backend *rb, const float *projection,
                       const struct vertex *verts, int num_verts) {
  /* uniforms will be lazily bound for each program inside of rb_draw_surface */
  rb->uniform_token++;
  rb->uniform_mvp = projection;

  glBindBuffer(GL_ARRAY_BUFFER, rb->ta_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(struct vertex) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  rb_bind_vao(rb, rb->ta_vao);

  if (rb->debug_wireframe) {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  }
}

void rb_end_surfaces2d(struct render_backend *rb) {}

void rb_draw_surface2d(struct render_backend *rb,
                       const struct surface2d *surf) {
  if (surf->scissor) {
    rb_set_scissor_test(rb, 1);
    rb_set_scissor_clip(rb, (int)surf->scissor_rect[0],
                        (int)surf->scissor_rect[1], (int)surf->scissor_rect[2],
                        (int)surf->scissor_rect[3]);
  } else {
    rb_set_scissor_test(rb, 0);
  }

  rb_set_blend_func(rb, surf->src_blend, surf->dst_blend);
  rb_bind_texture(rb, MAP_DIFFUSE,
                  surf->texture ? rb->textures[surf->texture] : rb->white_tex);

  if (rb->ui_use_ibo) {
    glDrawElements(prim_types[surf->prim_type], surf->num_verts,
                   GL_UNSIGNED_SHORT,
                   (void *)(intptr_t)(sizeof(uint16_t) * surf->first_vert));
  } else {
    glDrawArrays(prim_types[surf->prim_type], surf->first_vert,
                 surf->num_verts);
  }
}

void rb_begin_surfaces2d(struct render_backend *rb,
                         const struct vertex2d *verts, int num_verts,
                         uint16_t *indices, int num_indices) {
  glBindBuffer(GL_ARRAY_BUFFER, rb->ui_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(struct vertex2d) * num_verts, verts,
               GL_DYNAMIC_DRAW);

  if (indices) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rb->ui_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * num_indices,
                 indices, GL_DYNAMIC_DRAW);
    rb->ui_use_ibo = 1;
  } else {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, -1);
    rb->ui_use_ibo = 0;
  }
}

void rb_end_ortho(struct render_backend *rb) {
  rb_set_scissor_test(rb, 0);
}

void rb_begin_ortho(struct render_backend *rb) {
  float ortho[16];

  ortho[0] = 2.0f / (float)rb->window->width;
  ortho[4] = 0.0f;
  ortho[8] = 0.0f;
  ortho[12] = -1.0f;

  ortho[1] = 0.0f;
  ortho[5] = -2.0f / (float)rb->window->height;
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

  rb_set_depth_mask(rb, 0);
  rb_set_depth_func(rb, DEPTH_NONE);
  rb_set_cull_face(rb, CULL_NONE);

  rb_bind_vao(rb, rb->ui_vao);
  rb_bind_program(rb, &rb->ui_program);
  glUniformMatrix4fv(rb_get_uniform(rb, UNIFORM_MVP), 1, GL_FALSE, ortho);
}

void rb_end_frame(struct render_backend *rb) {
  SDL_GL_SwapWindow(rb->window->handle);
}

void rb_begin_frame(struct render_backend *rb) {
  rb_set_depth_mask(rb, 1);

  glViewport(0, 0, rb->window->width, rb->window->height);

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void rb_destroy_texture(struct render_backend *rb, texture_handle_t handle) {
  GLuint *gltex = &rb->textures[handle];
  glDeleteTextures(1, gltex);
  *gltex = 0;
}

texture_handle_t rb_create_texture(struct render_backend *rb,
                                   enum pxl_format format,
                                   enum filter_mode filter,
                                   enum wrap_mode wrap_u, enum wrap_mode wrap_v,
                                   int mipmaps, int width, int height,
                                   const uint8_t *buffer) {
  /* find next open texture handle */
  texture_handle_t handle;
  for (handle = 1; handle < MAX_TEXTURES; handle++) {
    if (!rb->textures[handle]) {
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

  GLuint *gltex = &rb->textures[handle];
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

void rb_destroy(struct render_backend *rb) {
  rb_destroy_vertex_buffers(rb);
  rb_destroy_shaders(rb);
  rb_destroy_textures(rb);

  if (rb->ctx) {
    win_gl_destroy_context(rb->window, rb->ctx);
  }

  free(rb);
}

struct render_backend *rb_create(struct window *window) {
  struct render_backend *rb = calloc(1, sizeof(struct render_backend));
  rb->window = window;

  rb->ctx = win_gl_create_context(rb->window);
  if (!rb->ctx) {
    rb_destroy(rb);
    return NULL;
  }

  rb_create_textures(rb);
  rb_create_shaders(rb);
  rb_create_vertex_buffers(rb);
  rb_set_initial_state(rb);

  return rb;
}
