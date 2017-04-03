#include <stdlib.h>
#include "ui/nuklear.h"
#include "core/core.h"
#include "core/log.h"
#include "core/string.h"
#include "ui/window.h"

#define NK_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4116)
#endif
#include <nuklear.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

static void nk_keydown(void *data, int device_index, enum keycode code,
                       int16_t value) {
  struct nuklear *nk = data;

  if (code == K_MWHEELUP) {
    nk->mouse_wheel = 1;
  } else if (code == K_MWHEELDOWN) {
    nk->mouse_wheel = -1;
  } else if (code == K_MOUSE1) {
    nk->mouse_down[0] = value > 0;
  } else if (code == K_MOUSE2) {
    nk->mouse_down[1] = value > 0;
  } else if (code == K_MOUSE3) {
    nk->mouse_down[2] = value > 0;
  } else if (code == K_LALT || code == K_RALT) {
    /*nk->alt[code == K_LALT ? 0 : 1] = !!value;
    io.KeyAlt = nk->alt[0] || nk->alt[1];*/
  } else if (code == K_LCTRL || code == K_RCTRL) {
    /*nk->ctrl[code == K_LCTRL ? 0 : 1] = !!value;
    io.KeyCtrl = nk->ctrl[0] || nk->ctrl[1];*/
  } else if (code == K_LSHIFT || code == K_RSHIFT) {
    /*nk->shift[code == K_LSHIFT ? 0 : 1] = !!value;
    io.KeyShift = nk->shift[0] || nk->shift[1];*/
  } else {
    /*io.KeysDown[code] = value > 0;*/
  }
}

static void nk_textinput(void *data, const char *text) {
  struct nuklear *nk = data;

  nk_glyph glyph;
  memcpy(glyph, text, NK_UTF_SIZE);
  nk_input_glyph(&nk->ctx, glyph);
}

static void nk_mousemove(void *data, int x, int y) {
  struct nuklear *nk = data;

  nk->mousex = x;
  nk->mousey = y;
}

void nk_end_frame(struct nuklear *nk) {
  struct render_backend *rb = nk->window->rb;

  /* convert draw list into vertex / element buffers */
  static const struct nk_draw_vertex_layout_element vertex_layout[] = {
      {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct vertex2d, xy)},
      {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct vertex2d, uv)},
      {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8,
       NK_OFFSETOF(struct vertex2d, color)},
      {NK_VERTEX_LAYOUT_END}};

  struct nk_buffer vbuf, ebuf;
  nk_buffer_init_fixed(&vbuf, nk->vertices, sizeof(nk->vertices));
  nk_buffer_init_fixed(&ebuf, nk->elements, sizeof(nk->elements));

  struct nk_convert_config config;
  memset(&config,0,sizeof(struct nk_convert_config));
  config.vertex_layout = vertex_layout;
  config.vertex_size = sizeof(struct vertex2d);
  config.vertex_alignment = NK_ALIGNOF(struct vertex2d);
  config.null = nk->null;
  config.global_alpha = 1.0f;
  config.shape_AA = NK_ANTI_ALIASING_OFF;
  config.line_AA = NK_ANTI_ALIASING_OFF;

  nk_convert(&nk->ctx, &nk->cmds, &vbuf, &ebuf, &config);

  /* bind buffers */
  rb_begin_ortho(rb);
  rb_begin_surfaces2d(rb, nk->vertices, nk->ctx.draw_list.vertex_count,
                      nk->elements, nk->ctx.draw_list.element_count);

  /* pass each draw command off to the render backend */
  const struct nk_draw_command *cmd = NULL;
  int offset = 0;

  struct surface2d surf = {0};
  surf.prim_type = PRIM_TRIANGLES;
  surf.src_blend = BLEND_SRC_ALPHA;
  surf.dst_blend = BLEND_ONE_MINUS_SRC_ALPHA;
  surf.scissor = 1;

  nk_draw_foreach(cmd, &nk->ctx, &nk->cmds) {
    if (!cmd->elem_count) {
      continue;
    }

    surf.texture = (texture_handle_t)cmd->texture.id;
    surf.scissor_rect[0] = cmd->clip_rect.x;
    surf.scissor_rect[1] =
        nk->window->height - (cmd->clip_rect.y + cmd->clip_rect.h);
    surf.scissor_rect[2] = cmd->clip_rect.w;
    surf.scissor_rect[3] = cmd->clip_rect.h;
    surf.first_vert = offset;
    surf.num_verts = cmd->elem_count;

    rb_draw_surface2d(rb, &surf);

    offset += cmd->elem_count;
  }
  nk_clear(&nk->ctx);

  rb_end_surfaces2d(rb);
  rb_end_ortho(rb);

  /* reset mouse wheel state as it won't be reset through any event */
  nk->mouse_wheel = 0;
}

void nk_begin_frame(struct nuklear *nk) {
  /* update input state for the frame */
  nk_input_begin(&nk->ctx);

  nk_input_motion(&nk->ctx, nk->mousex, nk->mousey);
  nk_input_scroll(&nk->ctx, (float)nk->mouse_wheel);
  nk_input_button(&nk->ctx, NK_BUTTON_LEFT, nk->mousex, nk->mousey,
                  nk->mouse_down[0]);
  nk_input_button(&nk->ctx, NK_BUTTON_MIDDLE, nk->mousex, nk->mousey,
                  nk->mouse_down[1]);
  nk_input_button(&nk->ctx, NK_BUTTON_RIGHT, nk->mousex, nk->mousey,
                  nk->mouse_down[2]);

  nk_input_end(&nk->ctx);
}

void nk_destroy(struct nuklear *nk) {
  nk_buffer_free(&nk->cmds);
  nk_font_atlas_clear(&nk->atlas);
  nk_free(&nk->ctx);

  rb_destroy_texture(nk->window->rb, nk->font_texture);

  win_remove_listener(nk->window, &nk->listener);

  free(nk);
}

struct nuklear *nk_create(struct window *window) {
  struct nuklear *nk = calloc(1, sizeof(struct nuklear));
  nk->window = window;
  nk->listener = (struct window_listener){
      nk,          NULL,          NULL,          NULL, NULL,
      &nk_keydown, &nk_textinput, &nk_mousemove, NULL, {0}};

  win_add_listener(nk->window, &nk->listener);

  /* create default font texture */
  nk_font_atlas_init_default(&nk->atlas);
  nk_font_atlas_begin(&nk->atlas);
  struct nk_font *font = nk_font_atlas_add_default(&nk->atlas, 13.0f, NULL);
  int font_width, font_height;
  const void *font_data = nk_font_atlas_bake(
      &nk->atlas, &font_width, &font_height, NK_FONT_ATLAS_RGBA32);
  nk->font_texture =
      rb_create_texture(nk->window->rb, PXL_RGBA, FILTER_BILINEAR, WRAP_REPEAT,
                        WRAP_REPEAT, 0, font_width, font_height, font_data);
  nk_font_atlas_end(&nk->atlas, nk_handle_id((int)nk->font_texture), &nk->null);

  /* initialize nuklear context */
  nk_init_default(&nk->ctx, &font->handle);
  nk_buffer_init_default(&nk->cmds);

  return nk;
}
