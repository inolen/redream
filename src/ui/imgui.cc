#include <imgui.h>
#include "renderer/backend.h"
#include "ui/imgui.h"
#include "ui/window.h"

typedef struct imgui_s {
  struct window_s *window;
  struct window_listener_s *listener;
  bool alt[2];
  bool ctrl[2];
  bool shift[2];
} imgui_t;

static void imgui_onprepaint(imgui_t *imgui) {
  ImGuiIO &io = ImGui::GetIO();

  int width = win_width(imgui->window);
  int height = win_height(imgui->window);
  io.DisplaySize =
      ImVec2(static_cast<float>(width), static_cast<float>(height));

  ImGui::NewFrame();

  // reset mouse scroll state
  io.MouseWheel = 0.0;
}

static void imgui_onpostpaint(imgui_t *imgui) {
  ImGuiIO &io = ImGui::GetIO();
  struct rb_s *rb = win_render_backend(imgui->window);

  // if there are any focused items, enable text input
  win_enable_text_input(imgui->window, ImGui::IsAnyItemActive());

  // update draw batches. note, this doesn't _actually_ render anything because
  // io.RenderDrawListsFn is null
  ImGui::Render();

  // get the latest draw batches, and pass them off out the render backend
  ImDrawData *data = ImGui::GetDrawData();

  rb_begin2d(rb);

  for (int i = 0; i < data->CmdListsCount; ++i) {
    const auto cmd_list = data->CmdLists[i];

    vertex2d_t *verts =
        reinterpret_cast<vertex2d_t *>(cmd_list->VtxBuffer.Data);
    int num_verts = cmd_list->VtxBuffer.size();

    uint16_t *indices = cmd_list->IdxBuffer.Data;
    int num_indices = cmd_list->IdxBuffer.size();

    rb_begin_surfaces2d(rb, verts, num_verts, indices, num_indices);

    int index_offset = 0;

    for (int j = 0; j < cmd_list->CmdBuffer.size(); ++j) {
      const auto &cmd = cmd_list->CmdBuffer[j];

      surface2d_t surf;
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

  rb_end2d(rb);
}

static void imgui_onkeydown(imgui_t *imgui, keycode_t code, int16_t value) {
  ImGuiIO &io = ImGui::GetIO();

  if (code == K_MWHEELUP) {
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
    imgui->alt[code == K_LALT ? 0 : 1] = !!value;
    io.KeyAlt = imgui->alt[0] || imgui->alt[1];
  } else if (code == K_LCTRL || code == K_RCTRL) {
    imgui->ctrl[code == K_LCTRL ? 0 : 1] = !!value;
    io.KeyCtrl = imgui->ctrl[0] || imgui->ctrl[1];
  } else if (code == K_LSHIFT || code == K_RSHIFT) {
    imgui->shift[code == K_LSHIFT ? 0 : 1] = !!value;
    io.KeyShift = imgui->shift[0] || imgui->shift[1];
  } else {
    io.KeysDown[code] = !!value;
  }
}

static void imgui_ontextinput(imgui_t *imgui, const char *text) {
  ImGuiIO &io = ImGui::GetIO();

  io.AddInputCharactersUTF8(text);
}

static void imgui_onmousemove(imgui_t *imgui, int x, int y) {
  ImGuiIO &io = ImGui::GetIO();

  io.MousePos = ImVec2((float)x, (float)y);
}

imgui_t *imgui_create(window_s *window) {
  static const window_callbacks_t callbacks = {
      (window_prepaint_cb)&imgui_onprepaint,
      NULL,
      (window_postpaint_cb)&imgui_onpostpaint,
      (window_keydown_cb)&imgui_onkeydown,
      (window_textinput_cb)&imgui_ontextinput,
      (window_mousemove_cb)&imgui_onmousemove,
      NULL};

  imgui_t *imgui = reinterpret_cast<imgui_t *>(calloc(1, sizeof(imgui_t)));

  imgui->window = window;
  imgui->listener = win_add_listener(imgui->window, &callbacks, imgui);

  // initialize imgui
  ImGuiIO &io = ImGui::GetIO();
  struct rb_s *rb = win_render_backend(imgui->window);

  // don't really care if this is accurate
  io.DeltaTime = 1.0f / 60.0f;

  // don't save settings
  io.IniSavingRate = 0.0f;

  // setup key mapping
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
  io.Fonts->TexID = reinterpret_cast<void *>(static_cast<intptr_t>(handle));

  return imgui;
}

void imgui_destroy(imgui_t *imgui) {
  win_remove_listener(imgui->window, imgui->listener);

  ImGui::Shutdown();

  free(imgui);
}
