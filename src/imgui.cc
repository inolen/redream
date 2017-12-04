#define IMGUI_IMPLEMENTATION
#define IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_PLACEMENT_NEW
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

extern "C" {
#include <zlib.h>
#include "core/core.h"
#include "core/time.h"
#include "host/keycode.h"
#include "imgui.h"
#include "render/render_backend.h"
}

#define IMFONT_MAX_HEIGHT 128

struct imgui {
  struct render_backend *r;
  struct ImFont *fonts[IMFONT_NUM_FONTS][IMFONT_MAX_HEIGHT];
  int64_t time;
  int alt[2];
  int ctrl[2];
  int shift[2];
  int keys[K_NUM_KEYS];
};

/* maintain global pointer for ig* functions */
static struct imgui *g_imgui;
#include "assets/fontawesome-webfont.inc"
#include "assets/opensans-regular.inc"
#include "assets/oswald-medium.inc"

static ImFont *imgui_get_font(struct imgui *imgui, int id, int font_height);

/*
 * imgui extensions
 */
using namespace ImGui;

namespace ImGui {

static ImRect ButtonBox(const ImVec2 &label_size, const ImVec2 &req_size) {
  ImGuiContext &g = *GImGui;
  const ImGuiStyle &style = g.Style;
  ImGuiWindow *window = GetCurrentWindow();
  ImVec2 pos = window->DC.CursorPos;
  ImVec2 size =
      CalcItemSize(req_size, label_size.x + style.FramePadding.x * 2.0f,
                   label_size.y + style.FramePadding.y * 2.0f);
  return ImRect(pos, pos + size);
}

static ImU32 SelectableColor(bool selected, bool hovered, bool held) {
  if (selected || (hovered && held)) {
    return ImGui::GetColorU32(ImGuiCol_ButtonActive);
  }

  if (hovered) {
    return ImGui::GetColorU32(ImGuiCol_ButtonHovered);
  }

  return ImGui::GetColorU32(ImGuiCol_Button);
}

static void RenderCircularNavHighlight(const ImRect &bb, ImGuiID id) {
  ImGuiContext &g = *GImGui;

  if (id != g.NavId || g.NavDisableHighlight) {
    return;
  }

  ImGuiWindow *window = GetCurrentWindow();
  const float THICKNESS = 2.0f;
  const float DISTANCE = 3.0f + THICKNESS * 0.5f;
  ImRect display_rect(bb.Min - ImVec2(DISTANCE, DISTANCE),
                      bb.Max + ImVec2(DISTANCE, DISTANCE));

  if (!window->ClipRect.Contains(display_rect)) {
    window->DrawList->PushClipRect(display_rect.Min, display_rect.Max);
  }

  const ImRect draw_rect(
      display_rect.Min + ImVec2(THICKNESS * 0.5f, THICKNESS * 0.5f),
      display_rect.Max - ImVec2(THICKNESS * 0.5f, THICKNESS * 0.5f));
  const ImVec2 center = draw_rect.GetCenter();
  float radius = draw_rect.GetWidth() / 2.0f;
  window->DrawList->AddCircle(
      center, radius, GetColorU32(ImGuiCol_NavHighlight), 48, THICKNESS);

  if (!window->ClipRect.Contains(display_rect)) {
    window->DrawList->PopClipRect();
  }
}
}

int igDiscButton(ImTextureID user_texture_id, float item_diameter,
                 float draw_diameter, const struct ImVec2 uv0,
                 const struct ImVec2 uv1) {
  ImGuiWindow *window = GetCurrentWindow();

  if (window->SkipItems) {
    return 0;
  }

  ImGuiContext &g = *GImGui;
  const ImGuiStyle &style = g.Style;

  ImGui::PushID((void *)user_texture_id);
  const ImGuiID id = window->GetID("#image");
  ImGui::PopID();

  const ImVec2 item_size = {item_diameter, item_diameter};
  const ImVec2 item_pos = window->DC.CursorPos;
  const ImRect item_bb(item_pos, item_pos + item_size);

  const ImVec2 draw_size = {draw_diameter, draw_diameter};
  const ImVec2 draw_pos = {item_pos.x - (draw_diameter - item_diameter) / 2.0f,
                           item_pos.y - (draw_diameter - item_diameter) / 2.0f};
  const ImRect draw_bb(draw_pos, draw_pos + draw_size);

  ImGui::ItemSize(item_bb);

  if (!ImGui::ItemAdd(item_bb, id)) {
    return 0;
  }

  bool hovered, held;
  bool pressed = ButtonBehavior(item_bb, id, &hovered, &held);
  ImGui::RenderCircularNavHighlight(draw_bb, id);
  window->DrawList->AddImage(user_texture_id, draw_bb.Min, draw_bb.Max, uv0,
                             uv1, 0xffffffff);

  return (int)pressed;
}

int igOptionString(const char *label, const char *value, struct ImVec2 size) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();

  if (window->SkipItems) {
    return 0;
  }

  ImGuiContext &g = *GImGui;
  const ImGuiStyle &style = g.Style;
  const ImGuiID id = window->GetID(label);
  const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
  const ImVec2 value_size = ImGui::CalcTextSize(value, NULL, true);
  const ImVec2 total_size(label_size.x + value_size.x,
                          MAX(label_size.y, value_size.y));
  const ImRect bb = ImGui::ButtonBox(total_size, size);

  ImGui::ItemSize(bb, style.FramePadding.y);

  if (!ImGui::ItemAdd(bb, id)) {
    return 0;
  }

  ImGuiButtonFlags flags = 0;
  if (window->DC.ItemFlags & ImGuiItemFlags_ButtonRepeat) {
    flags |= ImGuiButtonFlags_Repeat;
  }
  bool hovered, held;
  bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

  const ImU32 col = SelectableColor(false, hovered, held);
  ImGui::RenderNavHighlight(bb, id);
  ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);
  ImGui::RenderTextClipped(bb.Min + style.FramePadding,
                           bb.Max - style.FramePadding, label, NULL,
                           &label_size, ImVec2(0.0f, 0.5f), &bb);
  ImGui::RenderTextClipped(bb.Min + style.FramePadding,
                           bb.Max - style.FramePadding, value, NULL,
                           &value_size, ImVec2(1.0f, 0.5f), &bb);

  return (int)pressed;
}

int igOptionInt(const char *label, int value, struct ImVec2 size) {
  char value_str[128];
  snprintf(value_str, sizeof(value_str), "%d", value);
  return igOptionString(label, value_str, size);
}

int igTab(const char *label, struct ImVec2 size, int selected) {
  ImGuiWindow *window = ImGui::GetCurrentWindow();

  if (window->SkipItems) {
    return 0;
  }

  ImGuiContext &g = *GImGui;
  const ImGuiStyle &style = g.Style;
  const ImGuiID id = window->GetID(label);
  const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
  const ImRect bb = ImGui::ButtonBox(label_size, size);

  ImGui::ItemSize(bb, style.FramePadding.y);

  if (!ImGui::ItemAdd(bb, id)) {
    return 0;
  }

  ImGuiButtonFlags flags = 0;
  if (window->DC.ItemFlags & ImGuiItemFlags_ButtonRepeat) {
    flags |= ImGuiButtonFlags_Repeat;
  }
  bool hovered, held;
  bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

  const ImU32 col = SelectableColor(selected, hovered, held);
  ImGui::RenderNavHighlight(bb, id);
  ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);
  ImGui::RenderTextClipped(bb.Min + style.FramePadding,
                           bb.Max - style.FramePadding, label, NULL,
                           &label_size, style.ButtonTextAlign, &bb);

  return (int)pressed;
}

void igPushFontEx(int id, int font_height) {
  ImGuiIO &io = ImGui::GetIO();

  ImFont *font = imgui_get_font(g_imgui, id, font_height);
  ImGui::PushFont(font);
}

/*
 * imgui implementation
 */
static void imgui_update_font_tex(struct imgui *imgui) {
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

  font_tex = r_create_texture(imgui->r, PXL_RGBA, FILTER_BILINEAR, WRAP_REPEAT,
                              WRAP_REPEAT, 0, width, height, pixels);
  io.Fonts->TexID = (void *)(intptr_t)font_tex;
}

static ImFont *imgui_get_font(struct imgui *imgui, int id, int font_height) {
  CHECK(id >= 0 && id < IMFONT_NUM_FONTS);
  CHECK(font_height >= 0 && font_height < IMFONT_MAX_HEIGHT);

  ImFont **font = &g_imgui->fonts[id][font_height];

  if (*font) {
    return *font;
  }

  int font_len = 0;
  int font_gz_len = 0;
  const uint8_t *font_gz = NULL;

  switch (id) {
    case IMFONT_OSWALD_MEDIUM:
      font_len = oswald_medium_len;
      font_gz_len = oswald_medium_gz_len;
      font_gz = oswald_medium_gz;
      break;
    case IMFONT_OPENSANS_REGULAR:
      font_len = opensans_regular_len;
      font_gz_len = opensans_regular_len;
      font_gz = opensans_regular_gz;
      break;
    default:
      LOG_FATAL("igPushFontEx unsupported font %d", id);
      break;
  }

  ImGuiIO &io = ImGui::GetIO();

  /* load base font. note, AddFontFromMemoryTTF takes ownership of font_data, so
     it doesn't need to be freed */
  {
    unsigned long tmp_len = font_len;
    uint8_t *font_data = (uint8_t *)malloc(tmp_len);
    int res = uncompress(font_data, &tmp_len, font_gz, font_gz_len);
    CHECK_EQ(res, Z_OK);
    *font = io.Fonts->AddFontFromMemoryTTF(font_data, tmp_len,
                                           (float)font_height, NULL, NULL);
    CHECK_NOTNULL(*font);
  }

  /* merge fontawesome icons */
  {
    static const ImWchar fa_ranges[] = {IMICON_RANGES, 0};
    ImFontConfig config;
    config.MergeMode = true;

    unsigned long tmp_len = fontawesome_webfont_len;
    uint8_t *font_data = (uint8_t *)malloc(tmp_len);
    int res = uncompress(font_data, &tmp_len, fontawesome_webfont_gz,
                         fontawesome_webfont_gz_len);
    CHECK_EQ(res, Z_OK);
    *font = io.Fonts->AddFontFromMemoryTTF(
        font_data, tmp_len, (float)font_height, &config, fa_ranges);
    CHECK_NOTNULL(*font);
  }

  imgui_update_font_tex(imgui);

  return *font;
}

void imgui_end_frame(struct imgui *imgui) {
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
      surf.scissor = 1;
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
}

void imgui_begin_frame(struct imgui *imgui) {
  int64_t now = time_nanoseconds();
  int64_t delta_time = now - imgui->time;
  imgui->time = now;

  ImGuiIO &io = ImGui::GetIO();

  int width = r_width(imgui->r);
  int height = r_height(imgui->r);

  io.DeltaTime = delta_time / (float)NS_PER_SEC;
  io.MouseWheel = 0.0;
  io.DisplaySize = ImVec2((float)width, (float)height);

  /* update navigation inputs */
  io.NavInputs[ImGuiNavInput_PadActivate] = (imgui->keys[K_CONT_A] != 0);
  io.NavInputs[ImGuiNavInput_PadCancel] = (imgui->keys[K_CONT_B] != 0);
  io.NavInputs[ImGuiNavInput_PadUp] =
      (imgui->keys[K_CONT_DPAD_UP] != 0) || (imgui->keys[K_CONT_JOYY] < 0);
  io.NavInputs[ImGuiNavInput_PadDown] =
      (imgui->keys[K_CONT_DPAD_DOWN] != 0) || (imgui->keys[K_CONT_JOYY] > 0);
  io.NavInputs[ImGuiNavInput_PadLeft] =
      (imgui->keys[K_CONT_DPAD_LEFT] != 0) || (imgui->keys[K_CONT_JOYX] < 0);
  io.NavInputs[ImGuiNavInput_PadRight] =
      (imgui->keys[K_CONT_DPAD_RIGHT] != 0) || (imgui->keys[K_CONT_JOYX] > 0);

  ImGui::NewFrame();
}

int imgui_keydown(struct imgui *imgui, int key, int16_t value) {
  ImGuiIO &io = ImGui::GetIO();

  /* digital inputs will always be either 0 or INT_MAX, but analog inputs will
     range from INT16_MIN to INT16_MAX. filter small values to require very
     intentional actions when using these analog inputs for navigation */
  const int16_t min = 16384;
  if (value > min) {
    value = 1;
  } else if (value < -min) {
    value = -1;
  } else {
    value = 0;
  }

  bool down = value != 0;

  if (key == K_MWHEELUP) {
    io.MouseWheel = 1.0f;
  } else if (key == K_MWHEELDOWN) {
    io.MouseWheel = -1.0f;
  } else if (key == K_MOUSE1) {
    io.MouseDown[0] = down;
  } else if (key == K_MOUSE2) {
    io.MouseDown[1] = down;
  } else if (key == K_MOUSE3) {
    io.MouseDown[2] = down;
  } else if (key == K_LALT || key == K_RALT) {
    imgui->alt[key == K_LALT ? 0 : 1] = down;
    io.KeyAlt = imgui->alt[0] || imgui->alt[1];
  } else if (key == K_LCTRL || key == K_RCTRL) {
    imgui->ctrl[key == K_LCTRL ? 0 : 1] = down;
    io.KeyCtrl = imgui->ctrl[0] || imgui->ctrl[1];
  } else if (key == K_LSHIFT || key == K_RSHIFT) {
    imgui->shift[key == K_LSHIFT ? 0 : 1] = down;
    io.KeyShift = imgui->shift[0] || imgui->shift[1];
  } else {
    imgui->keys[key] = value;
    io.KeysDown[key] = down;
  }

  return 0;
}

void imgui_mousemove(struct imgui *imgui, int x, int y) {
  ImGuiIO &io = ImGui::GetIO();

  io.MousePos = ImVec2((float)x, (float)y);
}

void imgui_destroy(struct imgui *imgui) {
  ImGui::Shutdown();

  free(imgui);
}

void imgui_vid_destroyed(struct imgui *imgui) {
  ImGuiIO &io = ImGui::GetIO();

  /* free up cached font data */
  io.Fonts->Clear();
  memset(imgui->fonts, 0, sizeof(imgui->fonts));
  imgui_update_font_tex(imgui);

  imgui->r = NULL;
}

void imgui_vid_created(struct imgui *imgui, struct render_backend *r) {
  ImGuiIO &io = ImGui::GetIO();

  imgui->r = r;

  /* register default font */
  io.Fonts->AddFontDefault();
  imgui_update_font_tex(imgui);
}

struct imgui *imgui_create() {
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

  /* lessen navigation biasing */
  io.NavScoreScaleX = 0.5f;

  g_imgui = imgui;

  return imgui;
}
