#define MICROPROFILE_WEBSERVER 0
#define MICROPROFILE_GPU_TIMERS 0
#define MICROPROFILE_ENABLED 1
#define MICROPROFILEUI_ENABLED 1
#define MICROPROFILE_IMPL 1
#define MICROPROFILEUI_IMPL 1
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (1024 * 1024 * 20)
#define MICROPROFILE_CONTEXT_SWITCH_TRACE 0
#include <microprofile.h>
#include <microprofileui.h>

extern "C" {
#include "core/assert.h"
#include "core/math.h"
#include "renderer/backend.h"
#include "ui/microprofile.h"
#include "ui/window.h"
}

static const int FONT_WIDTH = 1024;
static const int FONT_HEIGHT = 9;
#include "ui/microprofile_font.inc"

static const int MAX_2D_VERTICES = 16384;
static const int MAX_2D_SURFACES = 256;

struct microprofile {
  struct window *window;
  struct window_listener *listener;
  texture_handle_t font_tex;
  struct surface2d surfs[MAX_2D_SURFACES];
  int num_surfs;
  struct vertex2d verts[MAX_2D_VERTICES];
  int num_verts;
};

static struct microprofile *s_mp;

#define Q0(d, member, v) d[0].member = v
#define Q1(d, member, v) \
  d[1].member = v;       \
  d[3].member = v
#define Q2(d, member, v) d[4].member = v
#define Q3(d, member, v) \
  d[2].member = v;       \
  d[5].member = v

static void mp_keydown(void *data, enum keycode code, int16_t value) {
  if (code == K_F2) {
    if (value) {
      MicroProfileToggleDisplayMode();
    }
  } else if (code == K_MOUSE1) {
    MicroProfileMouseButton(value, 0);
  } else if (code == K_MOUSE2) {
    MicroProfileMouseButton(0, value);
  }
}

static void mp_mousemove(void *data, int x, int y) {
  MicroProfileMousePosition(x, y, 0);
}

static struct vertex2d *mp_alloc_verts(struct microprofile *mp,
                                       const struct surface2d &desc,
                                       int count) {
  CHECK(mp->num_verts + count <= MAX_2D_VERTICES);
  uint32_t first_vert = mp->num_verts;
  mp->num_verts += count;

  // try to batch with the last surface if possible
  if (mp->num_surfs) {
    struct surface2d &last_surf = mp->surfs[mp->num_surfs - 1];

    if (last_surf.prim_type == desc.prim_type &&
        last_surf.texture == desc.texture &&
        last_surf.src_blend == desc.src_blend &&
        last_surf.dst_blend == desc.dst_blend) {
      last_surf.num_verts += count;
      return &mp->verts[first_vert];
    }
  }

  // else, allocate a new surface
  CHECK(mp->num_surfs < MAX_2D_SURFACES);
  struct surface2d &next_surf = mp->surfs[mp->num_surfs];
  next_surf.prim_type = desc.prim_type;
  next_surf.texture = desc.texture;
  next_surf.src_blend = desc.src_blend;
  next_surf.dst_blend = desc.dst_blend;
  next_surf.scissor = false;
  next_surf.first_vert = first_vert;
  next_surf.num_verts = count;
  mp->num_surfs++;
  return &mp->verts[first_vert];
}

static void mp_draw_text(struct microprofile *mp, int x, int y, uint32_t color,
                         const char *text) {
  float fx = static_cast<float>(x);
  float fy = static_cast<float>(y);
  float fy2 = fy + (MICROPROFILE_TEXT_HEIGHT + 1);
  int text_len = static_cast<int>(strlen(text));

  struct vertex2d *vertex = mp_alloc_verts(mp, {PRIM_TRIANGLES,
                                                mp->font_tex,
                                                BLEND_SRC_ALPHA,
                                                BLEND_ONE_MINUS_SRC_ALPHA,
                                                false,
                                                {0.0f, 0.0f, 0.0f, 0.0f},
                                                0,
                                                0},
                                           6 * text_len);

  for (int i = 0; i < text_len; i++) {
    float fx2 = fx + MICROPROFILE_TEXT_WIDTH;
    float fu = s_font_offsets[static_cast<int>(text[i])] /
               static_cast<float>(FONT_WIDTH);
    float fu2 = fu + MICROPROFILE_TEXT_WIDTH / static_cast<float>(FONT_WIDTH);

    Q0(vertex, xy[0], fx);
    Q0(vertex, xy[1], fy);
    Q0(vertex, color, color);
    Q0(vertex, uv[0], fu);
    Q0(vertex, uv[1], 0.0f);

    Q1(vertex, xy[0], fx2);
    Q1(vertex, xy[1], fy);
    Q1(vertex, color, color);
    Q1(vertex, uv[0], fu2);
    Q1(vertex, uv[1], 0.0f);

    Q2(vertex, xy[0], fx2);
    Q2(vertex, xy[1], fy2);
    Q2(vertex, color, color);
    Q2(vertex, uv[0], fu2);
    Q2(vertex, uv[1], 1.0f);

    Q3(vertex, xy[0], fx);
    Q3(vertex, xy[1], fy2);
    Q3(vertex, color, color);
    Q3(vertex, uv[0], fu);
    Q3(vertex, uv[1], 1.0f);

    fx = fx2 + 1.0f;

    vertex += 6;
  }
}

static void mp_draw_box(struct microprofile *mp, int x0, int y0, int x1, int y1,
                        uint32_t color, enum box_type type) {
  struct vertex2d *vertex = mp_alloc_verts(mp, {PRIM_TRIANGLES,
                                                0,
                                                BLEND_SRC_ALPHA,
                                                BLEND_ONE_MINUS_SRC_ALPHA,
                                                false,
                                                {0.0f, 0.0f, 0.0f, 0.0f},
                                                0,
                                                0},
                                           6);

  if (type == BOX_FLAT) {
    Q0(vertex, xy[0], (float)x0);
    Q0(vertex, xy[1], (float)y0);
    Q0(vertex, color, color);
    Q1(vertex, xy[0], (float)x1);
    Q1(vertex, xy[1], (float)y0);
    Q1(vertex, color, color);
    Q2(vertex, xy[0], (float)x1);
    Q2(vertex, xy[1], (float)y1);
    Q2(vertex, color, color);
    Q3(vertex, xy[0], (float)x0);
    Q3(vertex, xy[1], (float)y1);
    Q3(vertex, color, color);
  } else {
    uint32_t a = (color & 0xff000000) >> 24;
    uint32_t r = (color & 0xff0000) >> 16;
    uint32_t g = (color & 0xff00) >> 8;
    uint32_t b = color & 0xff;
    uint32_t max = MAX(MAX(MAX(r, g), b), 30u);
    uint32_t min = MIN(MIN(MIN(r, g), b), 180u);

    uint32_t r0 = 0xff & ((r + max) / 2);
    uint32_t g0 = 0xff & ((g + max) / 2);
    uint32_t b0 = 0xff & ((b + max) / 2);
    uint32_t r1 = 0xff & ((r + min) / 2);
    uint32_t g1 = 0xff & ((g + min) / 2);
    uint32_t b1 = 0xff & ((b + min) / 2);
    uint32_t color0 = (a << 24) | (b0 << 16) | (g0 << 8) | r0;
    uint32_t color1 = (a << 24) | (b1 << 16) | (g1 << 8) | r1;

    Q0(vertex, xy[0], (float)x0);
    Q0(vertex, xy[1], (float)y0);
    Q0(vertex, color, color0);
    Q1(vertex, xy[0], (float)x1);
    Q1(vertex, xy[1], (float)y0);
    Q1(vertex, color, color0);
    Q2(vertex, xy[0], (float)x1);
    Q2(vertex, xy[1], (float)y1);
    Q2(vertex, color, color1);
    Q3(vertex, xy[0], (float)x0);
    Q3(vertex, xy[1], (float)y1);
    Q3(vertex, color, color1);
  }
}

static void mp_draw_line(struct microprofile *mp, float *verts, int num_verts,
                         uint32_t color) {
  CHECK(num_verts);

  struct vertex2d *vertex = mp_alloc_verts(mp, {PRIM_LINES,
                                                0,
                                                BLEND_SRC_ALPHA,
                                                BLEND_ONE_MINUS_SRC_ALPHA,
                                                false,
                                                {0.0f, 0.0f, 0.0f, 0.0f},
                                                0,
                                                0},
                                           2 * (num_verts - 1));

  for (int i = 0; i < num_verts - 1; ++i) {
    vertex[0].xy[0] = verts[i * 2];
    vertex[0].xy[1] = verts[i * 2 + 1];
    vertex[0].color = color;
    vertex[1].xy[0] = verts[(i + 1) * 2];
    vertex[1].xy[1] = verts[(i + 1) * 2 + 1];
    vertex[1].color = color;
    vertex += 2;
  }
}

void mp_begin_frame(struct microprofile *mp) {}

void mp_end_frame(struct microprofile *mp) {
  s_mp = mp;

  // update draw surfaces
  MicroProfileFlip();
  MicroProfileDraw(mp->window->width, mp->window->height);

  // render the surfaces
  struct rb *rb = mp->window->rb;

  rb_begin_ortho(rb);
  rb_begin_surfaces2d(rb, mp->verts, mp->num_verts, nullptr, 0);

  for (int i = 0; i < mp->num_surfs; i++) {
    struct surface2d *surf = &mp->surfs[i];
    rb_draw_surface2d(rb, surf);
  }

  rb_end_surfaces2d(rb);
  rb_end_ortho(rb);

  // reset surfaces
  mp->num_surfs = 0;
  mp->num_verts = 0;
}

struct microprofile *mp_create(struct window *window) {
  static const struct window_callbacks callbacks = {
      NULL, NULL, NULL, &mp_keydown, NULL, &mp_mousemove, NULL};

  struct microprofile *mp = reinterpret_cast<struct microprofile *>(
      calloc(1, sizeof(struct microprofile)));

  mp->window = window;
  mp->listener = win_add_listener(mp->window, &callbacks, mp);

  // init microprofile
  struct rb *rb = mp->window->rb;

  // register and enable gpu and runtime group by default
  uint16_t gpu_group = MicroProfileGetGroup("gpu", MicroProfileTokenTypeCpu);
  g_MicroProfile.nActiveGroupWanted |= 1ll << gpu_group;

  uint16_t runtime_group =
      MicroProfileGetGroup("runtime", MicroProfileTokenTypeCpu);
  g_MicroProfile.nActiveGroupWanted |= 1ll << runtime_group;

  // render time / average time bars by default
  g_MicroProfile.nBars |= MP_DRAW_TIMERS | MP_DRAW_AVERAGE | MP_DRAW_CALL_COUNT;

  // register the font texture
  mp->font_tex =
      rb_register_texture(rb, PXL_RGBA, FILTER_NEAREST, WRAP_CLAMP_TO_EDGE,
                          WRAP_CLAMP_TO_EDGE, false, FONT_WIDTH, FONT_HEIGHT,
                          reinterpret_cast<const uint8_t *>(s_font_data));

  return mp;
}

void mp_destroy(struct microprofile *mp) {
  win_remove_listener(mp->window, mp->listener);

  free(mp);
}

// microprofile expects the following three functions to be defined, they're
// called during MicroProfileDraw
void MicroProfileDrawText(int x, int y, uint32_t color, const char *text,
                          uint32_t len) {
  // microprofile provides 24-bit rgb values for text color
  color = 0xff000000 | color;

  // convert color from argb -> abgr
  color = (color & 0xff000000) | ((color & 0xff) << 16) | (color & 0xff00) |
          ((color & 0xff0000) >> 16);

  mp_draw_text(s_mp, x, y, color, text);
}

void MicroProfileDrawBox(int x0, int y0, int x1, int y1, uint32_t color,
                         MicroProfileBoxType type) {
  // convert color from argb -> abgr
  color = (color & 0xff000000) | ((color & 0xff) << 16) | (color & 0xff00) |
          ((color & 0xff0000) >> 16);

  mp_draw_box(s_mp, x0, y0, x1, y1, color, (enum box_type)type);
}

void MicroProfileDrawLine2D(uint32_t num_vertices, float *vertices,
                            uint32_t color) {
  // microprofile provides 24-bit rgb values for line color
  color = 0xff000000 | color;

  // convert color from argb -> abgr
  color = (color & 0xff000000) | ((color & 0xff) << 16) | (color & 0xff00) |
          ((color & 0xff0000) >> 16);

  mp_draw_line(s_mp, vertices, num_vertices, color);
}
