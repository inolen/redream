#ifdef HAVE_IMGUI
#define IMGUI_IMPLEMENTATION
#define IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_PLACEMENT_NEW
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#endif

extern "C" {
#include "core/core.h"
#include "core/time.h"
#include "host/keycode.h"
#include "imgui.h"
#include "render/render_backend.h"
}

struct imgui {
  struct render_backend *r;
  int64_t time;
  int alt[2];
  int ctrl[2];
  int shift[2];
  uint16_t keys[K_NUM_KEYS];
};

static void imgui_update_font_tex(struct imgui *imgui) {
#ifdef HAVE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  /* destroy old texture first */
  texture_handle_t font_tex = (texture_handle_t)(intptr_t)io.Fonts->TexID;
  if (font_tex) {
    r_destroy_texture(imgui->r, font_tex);
  }

  /* create new texture if fonts have been added */
  uint8_t *pixels;
  int width;
  int height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  if (!width || !height) {
    return;
  }

  LOG_INFO("imgui_update_font_tex w=%d h=%d", width, height);

  font_tex = r_create_texture(imgui->r, PXL_RGBA, FILTER_BILINEAR, WRAP_REPEAT,
                              WRAP_REPEAT, 0, width, height, pixels);
  io.Fonts->TexID = (void *)(intptr_t)font_tex;
#endif
}

void imgui_end_frame(struct imgui *imgui) {
#ifdef HAVE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  int width = (int)io.DisplaySize.x;
  int height = (int)io.DisplaySize.y;

  /* update draw batches. note, this doesn't _actually_ render anything because
     io.RenderDrawListsFn is null */
  ImGui::Render();

  /* get the latest draw batches, and pass them off out the render backend */
  ImDrawData *draw_data = ImGui::GetDrawData();

  r_viewport(imgui->r, 0, 0, width, height);

  for (int i = 0; i < draw_data->CmdListsCount; ++i) {
    const auto cmd_list = draw_data->CmdLists[i];

    struct ui_vertex *verts = (struct ui_vertex *)cmd_list->VtxBuffer.Data;
    int num_verts = cmd_list->VtxBuffer.size();

    uint16_t *indices = cmd_list->IdxBuffer.Data;
    int num_indices = cmd_list->IdxBuffer.size();

    r_begin_ui_surfaces(imgui->r, verts, num_verts, indices, num_indices);

    int index_offset = 0;

    for (int j = 0; j < cmd_list->CmdBuffer.size(); ++j) {
      const auto &cmd = cmd_list->CmdBuffer[j];

      struct ui_surface surf;
      surf.prim_type = PRIM_TRIANGLES;
      surf.texture = (texture_handle_t)(intptr_t)cmd.TextureId;
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

void imgui_begin_frame(struct imgui *imgui) {
  int64_t now = time_nanoseconds();
  int64_t delta_time = now - imgui->time;
  imgui->time = now;

#ifdef HAVE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  int width = r_width(imgui->r);
  int height = r_height(imgui->r);

  io.DeltaTime = delta_time / (float)NS_PER_SEC;
  io.MouseWheel = 0.0;
  io.DisplaySize = ImVec2((float)width, (float)height);

  ImGui::NewFrame();
#endif
}

int imgui_keydown(struct imgui *imgui, int key, uint16_t value) {
#ifdef HAVE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  if (key == K_MWHEELUP) {
    io.MouseWheel = 1.0f;
  } else if (key == K_MWHEELDOWN) {
    io.MouseWheel = -1.0f;
  } else if (key == K_MOUSE1) {
    io.MouseDown[0] = (bool)value;
  } else if (key == K_MOUSE2) {
    io.MouseDown[1] = (bool)value;
  } else if (key == K_MOUSE3) {
    io.MouseDown[2] = (bool)value;
  } else if (key == K_LALT || key == K_RALT) {
    imgui->alt[key == K_LALT ? 0 : 1] = value;
    io.KeyAlt = imgui->alt[0] || imgui->alt[1];
  } else if (key == K_LCTRL || key == K_RCTRL) {
    imgui->ctrl[key == K_LCTRL ? 0 : 1] = value;
    io.KeyCtrl = imgui->ctrl[0] || imgui->ctrl[1];
  } else if (key == K_LSHIFT || key == K_RSHIFT) {
    imgui->shift[key == K_LSHIFT ? 0 : 1] = value;
    io.KeyShift = imgui->shift[0] || imgui->shift[1];
  } else {
    imgui->keys[key] = value;
    io.KeysDown[key] = (bool)value;
  }
#endif
  return 0;
}

void imgui_mousemove(struct imgui *imgui, int x, int y) {
#ifdef HAVE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  io.MousePos = ImVec2((float)x, (float)y);
#endif
}

void imgui_destroy(struct imgui *imgui) {
#ifdef HAVE_IMGUI
  ImGui::Shutdown();

  free(imgui);
#endif
}

void imgui_vid_destroyed(struct imgui *imgui) {
#ifdef HAVE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  /* free up cached font data */
  io.Fonts->Clear();
  imgui_update_font_tex(imgui);

  imgui->r = NULL;
#endif
}

void imgui_vid_created(struct imgui *imgui, struct render_backend *r) {
#ifdef HAVE_IMGUI
  ImGuiIO &io = ImGui::GetIO();

  imgui->r = r;

  /* register default font */
  io.Fonts->AddFontDefault();
  imgui_update_font_tex(imgui);
#endif
}

struct imgui *imgui_create() {
#ifdef HAVE_IMGUI
  struct imgui *imgui =
      reinterpret_cast<struct imgui *>(calloc(1, sizeof(struct imgui)));

  /* initialize imgui */
  ImGuiIO &io = ImGui::GetIO();

  /* don't save settings */
  io.IniFilename = NULL;

  /* setup misc callbacks ImGui relies on */
  io.RenderDrawListsFn = nullptr;
  io.SetClipboardTextFn = nullptr;
  io.GetClipboardTextFn = nullptr;

  return imgui;
#else
  return NULL;
#endif
}
