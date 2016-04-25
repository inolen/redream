#include "ui/imgui_impl.h"
#include "ui/window.h"

using namespace re;
using namespace re::renderer;
using namespace re::ui;

ImGuiImpl::ImGuiImpl(Window &window) : window_(window) {
  window_.AddListener(this);
}

ImGuiImpl::~ImGuiImpl() {
  window_.RemoveListener(this);

  ImGui::Shutdown();
}

bool ImGuiImpl::Init() {
  ImGuiIO &io = ImGui::GetIO();
  Backend *rb = window_.render_backend();

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

  TextureHandle handle =
      rb->RegisterTexture(PXL_RGBA, FILTER_BILINEAR, WRAP_REPEAT, WRAP_REPEAT,
                          false, width, height, pixels);

  io.Fonts->TexID = reinterpret_cast<void *>(static_cast<intptr_t>(handle));

  return true;
}

void ImGuiImpl::OnPrePaint() {
  ImGuiIO &io = ImGui::GetIO();

  int width = window_.width();
  int height = window_.height();
  io.DisplaySize =
      ImVec2(static_cast<float>(width), static_cast<float>(height));

  ImGui::NewFrame();

  // reset mouse scroll state
  io.MouseWheel = 0.0;
}

void ImGuiImpl::OnPostPaint() {
  ImGuiIO &io = ImGui::GetIO();
  Backend *rb = window_.render_backend();

  // if there are any focused items, enable text input
  window_.EnableTextInput(ImGui::IsAnyItemActive());

  // update draw batches. note, this doesn't _actually_ render anything because
  // io.RenderDrawListsFn is null
  ImGui::Render();

  // get the latest draw batches, and pass them off out the render backend
  ImDrawData *data = ImGui::GetDrawData();

  rb->Begin2D();

  for (int i = 0; i < data->CmdListsCount; ++i) {
    const auto cmd_list = data->CmdLists[i];

    Vertex2D *verts = reinterpret_cast<Vertex2D *>(cmd_list->VtxBuffer.Data);
    int num_verts = cmd_list->VtxBuffer.size();

    uint16_t *indices = cmd_list->IdxBuffer.Data;
    int num_indices = cmd_list->IdxBuffer.size();

    rb->BeginSurfaces2D(verts, num_verts, indices, num_indices);

    int index_offset = 0;

    for (int j = 0; j < cmd_list->CmdBuffer.size(); ++j) {
      const auto &cmd = cmd_list->CmdBuffer[j];

      Surface2D surf;
      surf.prim_type = PRIM_TRIANGLES;
      surf.texture =
          static_cast<TextureHandle>(reinterpret_cast<intptr_t>(cmd.TextureId));
      surf.src_blend = BLEND_SRC_ALPHA;
      surf.dst_blend = BLEND_ONE_MINUS_SRC_ALPHA;
      surf.scissor = true;
      surf.scissor_rect[0] = cmd.ClipRect.x;
      surf.scissor_rect[1] = io.DisplaySize.y - cmd.ClipRect.w;
      surf.scissor_rect[2] = cmd.ClipRect.z - cmd.ClipRect.x;
      surf.scissor_rect[3] = cmd.ClipRect.w - cmd.ClipRect.y;
      surf.first_vert = index_offset;
      surf.num_verts = cmd.ElemCount;

      rb->DrawSurface2D(surf);

      index_offset += cmd.ElemCount;
    }

    rb->EndSurfaces2D();
  }

  rb->End2D();
}

void ImGuiImpl::OnTextInput(const char *text) {
  ImGuiIO &io = ImGui::GetIO();

  io.AddInputCharactersUTF8(text);
}

void ImGuiImpl::OnKeyDown(Keycode code, int16_t value) {
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
    alt_[code == K_LALT ? 0 : 1] = !!value;
    io.KeyAlt = alt_[0] || alt_[1];
  } else if (code == K_LCTRL || code == K_RCTRL) {
    ctrl_[code == K_LCTRL ? 0 : 1] = !!value;
    io.KeyCtrl = ctrl_[0] || ctrl_[1];
  } else if (code == K_LSHIFT || code == K_RSHIFT) {
    shift_[code == K_LSHIFT ? 0 : 1] = !!value;
    io.KeyShift = shift_[0] || shift_[1];
  } else {
    io.KeysDown[code] = !!value;
  }
}

void ImGuiImpl::OnMouseMove(int x, int y) {
  ImGuiIO &io = ImGui::GetIO();

  io.MousePos = ImVec2((float)x, (float)y);
}
