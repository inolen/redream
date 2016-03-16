#ifndef WINDOW_LISTENER_H
#define WINDOW_LISTENER_H

#include <stdint.h>
#include "ui/keycode.h"

namespace re {
namespace ui {

class WindowListener {
 public:
  virtual void OnPrePaint(){};
  virtual void OnPaint(bool show_main_menu){};
  virtual void OnPostPaint(){};
  virtual void OnKeyDown(Keycode code, int16_t value){};
  virtual void OnTextInput(const char *text) {}
  virtual void OnMouseMove(int x, int y){};
  virtual void OnClose(){};
};
}
}

#endif
