#ifndef SYSTEM_H
#define SYSTEM_H

#include <SDL.h>
#include "core/ring_buffer.h"
#include "renderer/gl_context.h"
#include "system/keys.h"

namespace dreavm {
namespace system {

enum {
  MAX_EVENTS = 1024,
  NUM_JOYSTICK_AXES = (K_AXIS15 - K_AXIS0) + 1,
  NUM_JOYSTICK_KEYS = (K_JOY31 - K_JOY0) + 1
};
enum SystemEventType { SE_NONE, SE_KEY, SE_MOUSEMOVE, SE_RESIZE };

struct SystemEvent {
  SystemEventType type;
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

class System : public renderer::GLContext {
 public:
  System();
  ~System();

  bool Init();
  void Tick();
  bool PollEvent(SystemEvent *ev);

  bool GLInit(int *width, int *height);
  void GLSwapBuffers();

 private:
  bool InitSDL();
  void DestroySDL();
  bool InitWindow();
  void DestroyWindow();
  bool InitInput();
  void DestroyInput();
  void InitJoystick();
  void DestroyJoystick();

  void QueueEvent(const SystemEvent &ev);

  Keycode TranslateSDLKey(SDL_Keysym keysym);
  void PumpSDLEvents();
  void PumpEvents();

  int video_width_;
  int video_height_;
  SDL_Window *window_;
  SDL_GLContext glcontext_;
  SDL_Joystick *joystick_;
  core::RingBuffer<SystemEvent, MAX_EVENTS> events_;
};
}
}

#endif
