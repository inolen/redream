#if ENABLE_IMGUI
#include <imgui/imgui.h>
#endif

extern "C" {
#include "core/assert.h"
#include "host/keycode.h"
#include "render/render_backend.h"
}

struct imgui {
  struct render_backend *r;
  bool alt[2];
  bool ctrl[2];
  bool shift[2];
};

extern "C" void imgui_render(struct imgui *imgui) {
#if ENABLE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  /* update draw batches. note, this doesn't _actually_ render anything because
     io.RenderDrawListsFn is null */
  ImGui::Render();

  /* get the latest draw batches, and pass them off out the render backend */
  ImDrawData *draw_data = ImGui::GetDrawData();

  for (int i = 0; i < draw_data->CmdListsCount; ++i) {
    const auto cmd_list = draw_data->CmdLists[i];

    struct ui_vertex *verts =
        reinterpret_cast<struct ui_vertex *>(cmd_list->VtxBuffer.Data);
    int num_verts = cmd_list->VtxBuffer.size();

    uint16_t *indices = cmd_list->IdxBuffer.Data;
    int num_indices = cmd_list->IdxBuffer.size();

    r_begin_ui_surfaces(imgui->r, verts, num_verts, indices, num_indices);

    int index_offset = 0;

    for (int j = 0; j < cmd_list->CmdBuffer.size(); ++j) {
      const auto &cmd = cmd_list->CmdBuffer[j];

      struct ui_surface surf;
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

      r_draw_ui_surface(imgui->r, &surf);

      index_offset += cmd.ElemCount;
    }

    r_end_ui_surfaces(imgui->r);
  }
#endif
}

extern "C" void imgui_begin_frame(struct imgui *imgui) {
#if ENABLE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  int width = r_viewport_width(imgui->r);
  int height = r_viewport_height(imgui->r);
  io.DisplaySize =
      ImVec2(static_cast<float>(width), static_cast<float>(height));

  ImGui::NewFrame();

  /* reset mouse scroll state */
  io.MouseWheel = 0.0;
#endif
}

extern "C" void imgui_keydown(struct imgui *imgui, enum keycode code,
                              int16_t value) {
#if ENABLE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  if (code == K_MWHEELUP) {
    io.MouseWheel = 1.0f;
  } else if (code == K_MWHEELDOWN) {
    io.MouseWheel = -1.0f;
  } else if (code == K_MOUSE1) {
    io.MouseDown[0] = value > 0;
  } else if (code == K_MOUSE2) {
    io.MouseDown[1] = value > 0;
  } else if (code == K_MOUSE3) {
    io.MouseDown[2] = value > 0;
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
    io.KeysDown[code] = value > 0;
  }
#endif
}

extern "C" void imgui_mousemove(struct imgui *imgui, int x, int y) {
#if ENABLE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  io.MousePos = ImVec2((float)x, (float)y);
#endif
}

extern "C" void imgui_destroy(struct imgui *imgui) {
#if ENABLE_IMGUI
  ImGui::Shutdown();

  free(imgui);
#endif
}

extern "C" struct imgui *imgui_create(struct render_backend *r) {
#if ENABLE_IMGUI
  struct imgui *imgui =
      reinterpret_cast<struct imgui *>(calloc(1, sizeof(struct imgui)));

  imgui->r = r;

  /* initialize imgui */
  ImGuiIO &io = ImGui::GetIO();

  /* don't really care if this is accurate */
  io.DeltaTime = 1.0f / 60.0f;

  /* don't save settings */
  io.IniFilename = NULL;

  /* setup key mapping */
  io.KeyMap[ImGuiKey_Tab] = K_TAB;
  io.KeyMap[ImGuiKey_LeftArrow] = K_LEFT;
  io.KeyMap[ImGuiKey_RightArrow] = K_RIGHT;
  io.KeyMap[ImGuiKey_UpArrow] = K_UP;
  io.KeyMap[ImGuiKey_DownArrow] = K_DOWN;
  io.KeyMap[ImGuiKey_PageUp] = K_PAGEUP;
  io.KeyMap[ImGuiKey_PageDown] = K_PAGEDOWN;
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

  /* setup misc callbacks ImGui relies on */
  io.RenderDrawListsFn = nullptr;
  io.SetClipboardTextFn = nullptr;
  io.GetClipboardTextFn = nullptr;

  /* register font in backend */
  uint8_t *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  texture_handle_t handle =
      r_create_texture(imgui->r, PXL_RGBA, FILTER_BILINEAR, WRAP_REPEAT,
                       WRAP_REPEAT, 0, width, height, pixels);
  io.Fonts->TexID = reinterpret_cast<void *>(static_cast<intptr_t>(handle));

  return imgui;
#else
  return NULL;
#endif
}
