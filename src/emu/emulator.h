#ifndef EMULATOR_H
#define EMULATOR_H

#include "hw/dreamcast.h"
#include "ui/window_listener.h"

namespace re {

namespace ui {
class Window;
}

namespace emu {

class Emulator : public ui::WindowListener {
 public:
  Emulator(ui::Window &window);
  ~Emulator();

  void Run(const char *path);

 private:
  bool CreateDreamcast();
  void DestroyDreamcast();

  bool LoadBios(const char *path);
  bool LoadFlash(const char *path);
  bool LaunchBIN(const char *path);
  bool LaunchGDI(const char *path);

  void OnPaint(bool show_main_menu) final;
  void OnKeyDown(ui::Keycode code, int16_t value) final;
  void OnClose() final;

  ui::Window &window_;
  hw::Dreamcast dc_;
  bool running_;
};
}
}

#endif
