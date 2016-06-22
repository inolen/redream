#include <stdlib.h>
#include "core/core.h"
#include "core/string.h"
#include "renderer/backend.h"
#include "ui/nuklear.h"
#include "ui/window.h"

#define NK_IMPLEMENTATION
#include <nuklear.h>

static void nuklear_onprepaint(void *data) {
  struct nuklear *nk = data;

  /*int width = win_width(nk->window);
  int height = win_height(nk->window);
  io.DisplaySize =
      ImVec2(static_cast<float>(width), static_cast<float>(height));

  ImGui::NewFrame();*/
}

static void nuklear_onpostpaint(void *data) {
  struct nuklear *nk = data;
  struct rb *rb = nk->window->rb;

  /*// if there are any focused items, enable text input
  win_enable_text_input(nk->window, ImGui::IsAnyItemActive());

  // update draw batches. note, this doesn't _actually_ render anything because
  // io.RenderDrawListsFn is null
  ImGui::Render();

  // get the latest draw batches, and pass them off out the render backend
  ImDrawData *draw_data = ImGui::GetDrawData();

  rb_begin2d(rb);

  for (int i = 0; i < draw_data->CmdListsCount; ++i) {
    const auto cmd_list = draw_data->CmdLists[i];

    struct vertex2d *verts =
        reinterpret_cast<struct vertex2d *>(cmd_list->VtxBuffer.Data);
    int num_verts = cmd_list->VtxBuffer.size();

    uint16_t *indices = cmd_list->IdxBuffer.Data;
    int num_indices = cmd_list->IdxBuffer.size();

    rb_begin_surfaces2d(rb, verts, num_verts, indices, num_indices);

    int index_offset = 0;

    for (int j = 0; j < cmd_list->CmdBuffer.size(); ++j) {
      const auto &cmd = cmd_list->CmdBuffer[j];

      struct surface2d surf;
      surf.prim_type = PRIM_TRIANGLES;
      surf.texture = static_cast<texture_handle_t>(
          reinterpret_cast<intptr_t>(cmd.TextureId));
      surf.src_blend = BLEND_SRC_ALPHA;
      surf.dst_blend = BLEND_ONE_MINUS_SRC_ALPHA;
      surf.scissor = true;
      surf.scissor_rect[0] = cmd.ClipRect.x;
      surf.scissor_rect[1] = io.DisplaySize.y - cmd.ClipRect.w;
      surf.scissor_rect[2] = cmd.ClipRect.z - cmd.ClipRect.x;
      surf.scissor_rect[3] = cmd.ClipRect.w - cmd.ClipRect.y;
      surf.first_vert = index_offset;
      surf.num_verts = cmd.ElemCount;

      rb_draw_surface2d(rb, &surf);

      index_offset += cmd.ElemCount;
    }

    rb_end_surfaces2d(rb);
  }

  rb_end2d(rb);*/

  const struct nk_draw_command *cmd;
  const nk_draw_index *offset = NULL;

  /* fill converting configuration */
  struct nk_convert_config config;
  memset(&config, 0, sizeof(config));
  config.global_alpha = 1.0f;
  config.shape_AA = NK_ANTI_ALIASING_ON;
  config.line_AA = NK_ANTI_ALIASING_ON;
  config.circle_segment_count = 22;
  config.curve_segment_count = 22;
  config.arc_segment_count = 22;
  config.null = nk->null;

  /* setup buffers to load vertices and elements */
  static struct vertex2d vertices[1024 * 10];
  static uint16_t elements[1024 * 10];

  struct nk_buffer vbuf, ebuf;
  nk_buffer_init_fixed(&vbuf, vertices, (size_t)sizeof(vertices));
  nk_buffer_init_fixed(&ebuf, elements, (size_t)sizeof(elements));
  nk_convert(&nk->ctx, &nk->cmds, &vbuf, &ebuf, &config);

  rb_begin2d(rb);

  rb_begin_surfaces2d(rb, vertices, nk->ctx.draw_list.vertex_count, elements,
                      nk->ctx.draw_list.element_count);

  int n = 0;
  /* iterate over and execute each draw command */
  nk_draw_foreach(cmd, &nk->ctx, &nk->cmds) {
    if (!cmd->elem_count) {
      continue;
    }

    printf("%d\n", cmd->elem_count);

    struct surface2d surf;
    surf.prim_type = PRIM_TRIANGLES;
    surf.texture = (texture_handle_t)cmd->texture.id;
    surf.src_blend = BLEND_SRC_ALPHA;
    surf.dst_blend = BLEND_ONE_MINUS_SRC_ALPHA;
    surf.scissor = true;
    surf.scissor_rect[0] = cmd->clip_rect.x;
    surf.scissor_rect[1] = nk->window->height - (cmd->clip_rect.y + cmd->clip_rect.h);
    surf.scissor_rect[2] = cmd->clip_rect.w;
    surf.scissor_rect[3] = cmd->clip_rect.h;
    surf.first_vert = n;
    surf.num_verts = cmd->elem_count;

    rb_draw_surface2d(rb, &surf);

    /*glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
    glScissor(
        (GLint)(cmd->clip_rect.x * scale.x),
        (GLint)((height - (GLint)(cmd->clip_rect.y + cmd->clip_rect.h)) *
    scale.y),
        (GLint)(cmd->clip_rect.w * scale.x),
        (GLint)(cmd->clip_rect.h * scale.y));
    glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count, GL_UNSIGNED_SHORT,
    offset);*/

    n += cmd->elem_count;
    offset += cmd->elem_count;
  }

  nk_clear(&nk->ctx);

  rb_end2d(rb);
}

static void nuklear_onkeydown(void *data, enum keycode code, int16_t value) {
  struct nuklear *nk = data;

  /*if (code == K_MWHEELUP) {
    io.MouseWheel = 1.0f;
  } else if (code == K_MWHEELDOWN) {
    io.MouseWheel = -1.0f;
  } else if (code == K_MOUSE1) {
    io.MouseDown[0] = !!value;
  } else if (code == K_MOUSE2) {
    io.MouseDown[1] = !!value;
  } else if (code == K_MOUSE3) {
    io.MouseDown[2] = !!value;
  } else if (code == K_LALT || code == K_RALT) {
    nk->alt[code == K_LALT ? 0 : 1] = !!value;
    io.KeyAlt = nk->alt[0] || nk->alt[1];
  } else if (code == K_LCTRL || code == K_RCTRL) {
    nk->ctrl[code == K_LCTRL ? 0 : 1] = !!value;
    io.KeyCtrl = nk->ctrl[0] || nk->ctrl[1];
  } else if (code == K_LSHIFT || code == K_RSHIFT) {
    nk->shift[code == K_LSHIFT ? 0 : 1] = !!value;
    io.KeyShift = nk->shift[0] || nk->shift[1];
  } else {
    io.KeysDown[code] = !!value;
  }*/
}

static void nuklear_ontextinput(void *data, const char *text) {
  struct nuklear *nk = data;

  //io.AddInputCharactersUTF8(text);
}

static void nuklear_onmousemove(void *data, int x, int y) {
  struct nuklear *nk = data;

  //io.MousePos = ImVec2((float)x, (float)y);
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

  /*// setup key mapping
  io.KeyMap[ImGuiKey_Tab] = K_TAB;
  io.KeyMap[ImGuiKey_LeftArrow] = K_LEFT;
  io.KeyMap[ImGuiKey_RightArrow] = K_RIGHT;
  io.KeyMap[ImGuiKey_UpArrow] = K_UP;
  io.KeyMap[ImGuiKey_DownArrow] = K_DOWN;
  io.KeyMap[ImGuiKey_PageUp] = K_PAGEUP;
  io.KeyMap[ImGuiKey_PageDown] = K_PAGEDOWN;
  io.KeyMap[ImGuiKey_Home] = K_HOME;
  io.KeyMap[ImGuiKey_End] = K_END;
  io.KeyMap[ImGuiKey_Delete] = K_DELETE;
  io.KeyMap[ImGuiKey_Backspace] = K_BACKSPACE;
  io.KeyMap[ImGuiKey_Enter] = K_RETURN;
  io.KeyMap[ImGuiKey_Escape] = K_ESCAPE;
  io.KeyMap[ImGuiKey_A] = 'a';
  io.KeyMap[ImGuiKey_C] = 'c';
  io.KeyMap[ImGuiKey_V] = 'v';
  io.KeyMap[ImGuiKey_X] = 'x';
  io.KeyMap[ImGuiKey_Y] = 'y';
  io.KeyMap[ImGuiKey_Z] = 'z';

  // setup misc callbacks ImGui relies on
  io.RenderDrawListsFn = nullptr;
  io.SetClipboardTextFn = nullptr;
  io.GetClipboardTextFn = nullptr;

  // register font in backend
  uint8_t *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  texture_handle_t handle =
      rb_register_texture(rb, PXL_RGBA, FILTER_BILINEAR, WRAP_REPEAT,
                          WRAP_REPEAT, false, width, height, pixels);
  io.Fonts->TexID = reinterpret_cast<void *>(static_cast<intptr_t>(handle));*/

  nk_init_default(&nk->ctx, 0);
  nk_buffer_init_default(&nk->cmds);

  struct rb *rb = nk->window->rb;
  nk_font_atlas_init_default(&nk->atlas);
  nk_font_atlas_begin(&nk->atlas);
  int w, h;
  const void *image =
      nk_font_atlas_bake(&nk->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
  texture_handle_t handle =
      rb_register_texture(rb, PXL_RGBA, FILTER_BILINEAR, WRAP_REPEAT,
                          WRAP_REPEAT, false, w, h, image);
  nk_font_atlas_end(&nk->atlas, nk_handle_id((int)handle), &nk->null);
  if (nk->atlas.default_font) {
    nk_style_set_font(&nk->ctx, &nk->atlas.default_font->handle);
  }
  return nk;
}

void nuklear_destroy(struct nuklear *nk) {
  nk_buffer_free(&nk->cmds);
  win_remove_listener(nk->window, nk->listener);
  free(nk);
}
