#ifndef SYSTEM_H
#define SYSTEM_H

#include <vector>
#include <SDL.h>
#include "renderer/backend.h"
#include "ui/keycode.h"
#include "ui/imgui_impl.h"
#include "ui/microprofile_impl.h"
#include "ui/window_listener.h"

namespace re {
namespace ui {

enum {
  NUM_JOYSTICK_AXES = (K_AXIS15 - K_AXIS0) + 1,
  NUM_JOYSTICK_KEYS = (K_JOY31 - K_JOY0) + 1,
};

class Window {
 public:
  SDL_Window *handle() { return window_; }
  renderer::Backend *render_backend() { return rb_; }

  int width() { return width_; }
  int height() { return height_; }

  Window();
  ~Window();

  bool Init();

  void AddListener(WindowListener *listener);
  void RemoveListener(WindowListener *listener);

  bool MainMenuEnabled();
  void EnableMainMenu(bool active);

  bool TextInputEnabled();
  void EnableTextInput(bool active);

  void PumpEvents();

 private:
  void HandlePaint();
  void HandleKeyDown(Keycode code, int16_t value);
  void HandleTextInput(const char *text);
  void HandleMouseMove(int x, int y);
  void HandleResize(int width, int height);
  void HandleClose();

  void InitJoystick();
  void DestroyJoystick();

  Keycode TranslateSDLKey(const SDL_Keysym &keysym);
  void PumpSDLEvents();

  std::vector<WindowListener *> listeners_;
  SDL_Window *window_;
  renderer::Backend *rb_;
  ImGuiImpl imgui_;
  MicroProfileImpl microprofile_;
  int width_;
  int height_;
  bool show_main_menu_;
  SDL_Joystick *joystick_;
};
}
}

#endif
