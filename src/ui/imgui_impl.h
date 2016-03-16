#ifndef IMGUI_IMPL_H
#define IMGUI_IMPL_H

#include <imgui.h>
#include "ui/window_listener.h"

namespace re {
namespace ui {

class Window;

class ImGuiImpl : public WindowListener {
 public:
  ImGuiImpl(Window &window);
  ~ImGuiImpl();

  bool Init();

 private:
  void OnPrePaint() final;
  void OnPostPaint() final;
  void OnKeyDown(Keycode code, int16_t value) final;
  void OnTextInput(const char *text) final;
  void OnMouseMove(int x, int y) final;

  Window &window_;
  bool alt_[2];
  bool ctrl_[2];
  bool shift_[2];
};
}
}

#endif
