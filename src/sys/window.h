#ifndef SYSTEM_H
#define SYSTEM_H

#include <SDL.h>
#include "core/ring_buffer.h"
#include "sys/keycode.h"

namespace dvm {
namespace sys {

enum {
  MAX_EVENTS = 1024,
  NUM_JOYSTICK_AXES = (K_AXIS15 - K_AXIS0) + 1,
  NUM_JOYSTICK_KEYS = (K_JOY31 - K_JOY0) + 1,
};

enum WindowEventType {
  WE_KEY,
  WE_MOUSEMOVE,
  WE_RESIZE,
  WE_QUIT,
};

struct WindowEvent {
  WindowEventType type;
  union {
    struct {
      Keycode code;
      int16_t value;
    } key;

    struct {
      int x, y;
    } mousemove;

    struct {
      int width;
      int height;
    } resize;
  };
};

class Window {
 public:
  SDL_Window *handle() { return window_; }
  int width() { return width_; }
  int height() { return height_; }

  Window();
  ~Window();

  bool Init();
  void PumpEvents();
  bool PollEvent(WindowEvent *ev);

 private:
  void InitJoystick();
  void DestroyJoystick();

  void QueueEvent(const WindowEvent &ev);

  Keycode TranslateSDLKey(SDL_Keysym keysym);
  void PumpSDLEvents();

  SDL_Window *window_;
  int width_;
  int height_;
  SDL_Joystick *joystick_;
  RingBuffer<WindowEvent> events_;
};
}
}

#endif
