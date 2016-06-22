#include <stdlib.h>
#include "ui/nuklear.h"
#include "core/core.h"
#include "core/string.h"
#include "ui/window.h"

#define NK_IMPLEMENTATION
#include <nuklear.h>

static void nuklear_onprepaint(void *data) {
  struct nuklear *nk = data;

  // update input state for the frame
  nk_input_begin(&nk->ctx);

  nk_input_motion(&nk->ctx, nk->mousex, nk->mousey);
  nk_input_button(&nk->ctx, NK_BUTTON_LEFT, nk->mousex, nk->mousey, nk->mouse_down[0]);
  nk_input_button(&nk->ctx, NK_BUTTON_MIDDLE, nk->mousex, nk->mousey, nk->mouse_down[1]);
  nk_input_button(&nk->ctx, NK_BUTTON_RIGHT, nk->mousex, nk->mousey, nk->mouse_down[2]);

  nk_input_end(&nk->ctx);
}

static void nuklear_onpostpaint(void *data) {
  struct nuklear *nk = data;
  struct rb *rb = nk->window->rb;

  // // if there are any focused items, enable text input
  // win_enable_text_input(nk->window, ImGui::IsAnyItemActive());

  // convert draw list into vertex / element buffers
  struct nk_buffer vbuf, ebuf;
  nk_buffer_init_fixed(&vbuf, nk->vertices, sizeof(nk->vertices));
  nk_buffer_init_fixed(&ebuf, nk->elements, sizeof(nk->elements));

  struct nk_convert_config config = {};
  config.global_alpha = 1.0f;
  config.shape_AA = NK_ANTI_ALIASING_ON;
  config.line_AA = NK_ANTI_ALIASING_ON;
  config.circle_segment_count = 22;
  config.curve_segment_count = 22;
  config.arc_segment_count = 22;
  config.null = nk->null;

  nk_convert(&nk->ctx, &nk->cmds, &vbuf, &ebuf, &config);

  // bind buffers
  rb_begin_ortho(rb);
  rb_begin_surfaces2d(rb, nk->vertices, nk->ctx.draw_list.vertex_count,
                      nk->elements, nk->ctx.draw_list.element_count);

  // pass each draw command off to the render backend
  const struct nk_draw_command *cmd = NULL;
  int offset = 0;

  struct surface2d surf = {};
  surf.prim_type = PRIM_TRIANGLES;
  surf.src_blend = BLEND_SRC_ALPHA;
  surf.dst_blend = BLEND_ONE_MINUS_SRC_ALPHA;
  surf.scissor = true;

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

  // reset mouse wheel state as it won't be reset through any event
  nk->mouse_wheel = 0;
}

static void nuklear_onkeydown(void *data, enum keycode code, int16_t value) {
  struct nuklear *nk = data;

  if (code == K_MWHEELUP) {
    nk->mouse_wheel = 1;
  } else if (code == K_MWHEELDOWN) {
    nk->mouse_wheel = -1;
  } else if (code == K_MOUSE1) {
    nk->mouse_down[0] = !!value;
  } else if (code == K_MOUSE2) {
    nk->mouse_down[1] = !!value;
  } else if (code == K_MOUSE3) {
    nk->mouse_down[2] = !!value;
  } else if (code == K_LALT || code == K_RALT) {
    //nk->alt[code == K_LALT ? 0 : 1] = !!value;
    //io.KeyAlt = nk->alt[0] || nk->alt[1];
  } else if (code == K_LCTRL || code == K_RCTRL) {
    //nk->ctrl[code == K_LCTRL ? 0 : 1] = !!value;
    //io.KeyCtrl = nk->ctrl[0] || nk->ctrl[1];
  } else if (code == K_LSHIFT || code == K_RSHIFT) {
    //nk->shift[code == K_LSHIFT ? 0 : 1] = !!value;
    //io.KeyShift = nk->shift[0] || nk->shift[1];
  } else {
    //io.KeysDown[code] = !!value;
  }
}

static void nuklear_ontextinput(void *data, const char *text) {
  struct nuklear *nk = data;

  nk_glyph glyph;
  memcpy(glyph, text, NK_UTF_SIZE);
  nk_input_glyph(&nk->ctx, glyph);
}

static void nuklear_onmousemove(void *data, int x, int y) {
  struct nuklear *nk = data;

  nk->mousex = x;
  nk->mousey = y;
}

struct nk_context *nuklear_context(struct nuklear *nk) {
  return &nk->ctx;
}

struct nuklear *nuklear_create(struct window *window) {
  static const struct window_callbacks callbacks = {&nuklear_onprepaint,
                                                    NULL,
                                                    &nuklear_onpostpaint,
                                                    &nuklear_onkeydown,
                                                    &nuklear_ontextinput,
                                                    &nuklear_onmousemove,
                                                    NULL};

  struct nuklear *nk = calloc(1, sizeof(struct nuklear));
  nk->window = window;
  nk->listener = win_add_listener(nk->window, &callbacks, nk);

  // create default font texture
  nk_font_atlas_init_default(&nk->atlas);
  nk_font_atlas_begin(&nk->atlas);
  struct nk_font *font = nk_font_atlas_add_default(&nk->atlas, 13.0f, NULL);
  int font_width, font_height;
  const void *font_data = nk_font_atlas_bake(
      &nk->atlas, &font_width, &font_height, NK_FONT_ATLAS_RGBA32);
  texture_handle_t handle = rb_register_texture(
      nk->window->rb, PXL_RGBA, FILTER_BILINEAR, WRAP_REPEAT, WRAP_REPEAT,
      false, font_width, font_height, font_data);
  nk_font_atlas_end(&nk->atlas, nk_handle_id((int)handle), &nk->null);

  // initialize nuklear context
  nk_init_default(&nk->ctx, &font->handle);
  nk_buffer_init_default(&nk->cmds);

  return nk;
}

void nuklear_destroy(struct nuklear *nk) {
  nk_buffer_free(&nk->cmds);
  nk_font_atlas_clear(&nk->atlas);
  nk_free(&nk->ctx);

  win_remove_listener(nk->window, nk->listener);

  free(nk);
}
