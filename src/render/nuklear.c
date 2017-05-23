#include <stdlib.h>
#include "render/nuklear.h"
#include "core/core.h"
#include "core/log.h"
#include "core/string.h"
#include "host.h"
#include "keycode.h"

#define NK_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4116)
#endif
#include <nuklear.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

void nk_render(struct nuklear *nk) {
  float height = (float)r_video_height(nk->r);

  /* convert draw list into vertex / element buffers */
  static const struct nk_draw_vertex_layout_element vertex_layout[] = {
      {NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct ui_vertex, xy)},
      {NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct ui_vertex, uv)},
      {NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8,
       NK_OFFSETOF(struct ui_vertex, color)},
      {NK_VERTEX_LAYOUT_END}};

  struct nk_convert_config config = {0};
  config.vertex_layout = vertex_layout;
  config.vertex_size = sizeof(struct ui_vertex);
  config.vertex_alignment = NK_ALIGNOF(struct ui_vertex);
  config.null = nk->null;
  config.global_alpha = 1.0f;
  config.shape_AA = NK_ANTI_ALIASING_OFF;
  config.line_AA = NK_ANTI_ALIASING_OFF;

  nk_convert(&nk->ctx, &nk->cmds, &nk->vbuf, &nk->ebuf, &config);

  /* bind buffers */
  const void *vertices = nk_buffer_memory_const(&nk->vbuf);
  const void *elements = nk_buffer_memory_const(&nk->ebuf);
  r_begin_ui_surfaces(nk->r, vertices, nk->ctx.draw_list.vertex_count, elements,
                      nk->ctx.draw_list.element_count);

  /* pass each draw command off to the render backend */
  const struct nk_draw_command *cmd = NULL;
  int offset = 0;

  struct ui_surface surf = {0};
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
    surf.scissor_rect[1] = height - (cmd->clip_rect.y + cmd->clip_rect.h);
    surf.scissor_rect[2] = cmd->clip_rect.w;
    surf.scissor_rect[3] = cmd->clip_rect.h;
    surf.first_vert = offset;
    surf.num_verts = cmd->elem_count;

    r_draw_ui_surface(nk->r, &surf);

    offset += cmd->elem_count;
  }
  nk_clear(&nk->ctx);

  r_end_ui_surfaces(nk->r);

  /* reset mouse wheel state at this point as it won't be reset through an
     actual input event */
  nk->mouse_wheel = 0;
}

void nk_update_input(struct nuklear *nk) {
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

void nk_mousemove(struct nuklear *nk, int x, int y) {
  nk->mousex = x;
  nk->mousey = y;
}

void nk_keydown(struct nuklear *nk, enum keycode key, int16_t value) {
  if (key == K_MWHEELUP) {
    nk->mouse_wheel = 1;
  } else if (key == K_MWHEELDOWN) {
    nk->mouse_wheel = -1;
  } else if (key == K_MOUSE1) {
    nk->mouse_down[0] = value > 0;
  } else if (key == K_MOUSE2) {
    nk->mouse_down[1] = value > 0;
  } else if (key == K_MOUSE3) {
    nk->mouse_down[2] = value > 0;
  } else if (key == K_LALT || key == K_RALT) {
    /*nk->alt[key == K_LALT ? 0 : 1] = !!value;
    io.KeyAlt = nk->alt[0] || nk->alt[1];*/
  } else if (key == K_LCTRL || key == K_RCTRL) {
    /*nk->ctrl[key == K_LCTRL ? 0 : 1] = !!value;
    io.KeyCtrl = nk->ctrl[0] || nk->ctrl[1];*/
  } else if (key == K_LSHIFT || key == K_RSHIFT) {
    /*nk->shift[key == K_LSHIFT ? 0 : 1] = !!value;
    io.KeyShift = nk->shift[0] || nk->shift[1];*/
  } else {
    /*io.KeysDown[key] = value > 0;*/
  }
}

void nk_destroy(struct nuklear *nk) {
  /* clean up font texture */
  r_destroy_texture(nk->r, nk->font_texture);
  nk_font_atlas_clear(&nk->atlas);

  /* destroy nuklear context */
  nk_buffer_free(&nk->ebuf);
  nk_buffer_free(&nk->vbuf);
  nk_buffer_free(&nk->cmds);
  nk_free(&nk->ctx);

  free(nk);
}

struct nuklear *nk_create(struct render_backend *r) {
  struct nuklear *nk = calloc(1, sizeof(struct nuklear));

  nk->r = r;

  /* create default font texture */
  nk_font_atlas_init_default(&nk->atlas);
  nk_font_atlas_begin(&nk->atlas);
  struct nk_font *font = nk_font_atlas_add_default(&nk->atlas, 13.0f, NULL);
  int font_width, font_height;
  const void *font_data = nk_font_atlas_bake(
      &nk->atlas, &font_width, &font_height, NK_FONT_ATLAS_RGBA32);
  nk->font_texture =
      r_create_texture(nk->r, PXL_RGBA, FILTER_BILINEAR, WRAP_REPEAT,
                       WRAP_REPEAT, 0, font_width, font_height, font_data);
  nk_font_atlas_end(&nk->atlas, nk_handle_id((int)nk->font_texture), &nk->null);

  /* initialize nuklear context */
  nk_init_default(&nk->ctx, &font->handle);
  nk_buffer_init_default(&nk->cmds);
  nk_buffer_init_default(&nk->vbuf);
  nk_buffer_init_default(&nk->ebuf);

  return nk;
}
