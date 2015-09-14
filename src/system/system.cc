#include <stdlib.h>
#include <GL/glew.h>
#include <SDL_opengl.h>
#include "core/core.h"
#include "system/system.h"

#define DEFAULT_VIDEO_WIDTH 800
#define DEFAULT_VIDEO_HEIGHT 600

using namespace dreavm;
using namespace dreavm::system;

static inline SystemEvent MakeKeyEvent(Keycode code, int16_t value) {
  SystemEvent ev;
  ev.type = SE_KEY;
  ev.key.code = code;
  ev.key.value = value;
  return ev;
}

static inline SystemEvent MakeMouseMoveEvent(int x, int y) {
  SystemEvent ev;
  ev.type = SE_MOUSEMOVE;
  ev.mousemove.x = x;
  ev.mousemove.y = y;
  return ev;
}

static inline SystemEvent MakeResizeEvent(int width, int height) {
  SystemEvent ev;
  ev.type = SE_RESIZE;
  ev.resize.width = width;
  ev.resize.height = height;
  return ev;
}

System::System()
    : video_width_(DEFAULT_VIDEO_WIDTH),
      video_height_(DEFAULT_VIDEO_HEIGHT),
      window_(nullptr),
      glcontext_(nullptr),
      joystick_(nullptr),
      events_(MAX_EVENTS) {}

System::~System() {
  GLDestroyContext();
  DestroyInput();
  DestroyWindow();
  DestroySDL();
}

bool System::Init() {
  if (!InitSDL()) {
    return false;
  }

  if (!InitWindow()) {
    return false;
  }

  if (!InitInput()) {
    return false;
  }

  return true;
}

void System::PumpEvents() { PumpSDLEvents(); }

bool System::PollEvent(SystemEvent *ev) {
  if (events_.Empty()) {
    return false;
  }

  *ev = events_.front();
  events_.PopFront();

  return true;
}

bool System::GLInitContext(int *width, int *height) {
  // need at least a 3.3 core context for our shaders
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  // request a 24-bit depth buffer. 16-bits isn't enough precision when
  // unprojecting dreamcast coordinates, see TileRenderer::GetProjectionMatrix
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  glcontext_ = SDL_GL_CreateContext(window_);
  if (!glcontext_) {
    LOG_WARNING("OpenGL context creation failed: %s");
    return false;
  }

  // link in gl functions at runtime
  glewExperimental = GL_TRUE;
  GLenum err = glewInit();
  if (err != GLEW_OK) {
    LOG_WARNING("GLEW initialization failed: %s", glewGetErrorString(err));
    return false;
  }

  // disable vsync
  SDL_GL_SetSwapInterval(0);

  *width = video_width_;
  *height = video_height_;

  return true;
}

void System::GLDestroyContext() {
  if (glcontext_) {
    SDL_GL_DeleteContext(glcontext_);
    glcontext_ = nullptr;
  }
}

void System::GLSwapBuffers() { SDL_GL_SwapWindow(window_); }

bool System::InitSDL() {
  if (SDL_Init(0) < 0) {
    LOG_WARNING("SDL initialization failed: %s", SDL_GetError());
    return false;
  }

  return true;
}

void System::DestroySDL() { SDL_Quit(); }

bool System::InitWindow() {
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
    LOG_WARNING("Video initialization failed: %s", SDL_GetError());
    return false;
  }

  window_ = SDL_CreateWindow(
      "dreavm", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, video_width_,
      video_height_, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!window_) {
    LOG_WARNING("Window creation failed: %s", SDL_GetError());
    return false;
  }

  return true;
}

void System::DestroyWindow() {
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
}

bool System::InitInput() {
  if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) < 0) {
    LOG_WARNING("Input initialization failed: %s", SDL_GetError());
    return false;
  }

  return true;
}

void System::DestroyInput() {
  DestroyJoystick();

  SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

void System::InitJoystick() {
  DestroyJoystick();

  // open the first connected joystick
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    joystick_ = SDL_JoystickOpen(i);

    if (joystick_) {
      LOG_INFO("Opened joystick %s (%d)", SDL_JoystickName(joystick_), i);
      break;
    }
  }
}

void System::DestroyJoystick() {
  if (joystick_) {
    SDL_JoystickClose(joystick_);
    joystick_ = nullptr;
  }
}

void System::QueueEvent(const SystemEvent &ev) {
  if (events_.Full()) {
    LOG_WARNING("System event overflow");
    return;
  }

  events_.PushBack(ev);
}

Keycode System::TranslateSDLKey(SDL_Keysym keysym) {
  Keycode out = K_UNKNOWN;

  if (keysym.sym >= SDLK_SPACE && keysym.sym <= SDLK_z) {
    // this range maps 1:1 with ASCII chars
    out = (Keycode)keysym.sym;
  } else {
    switch (keysym.sym) {
      case SDLK_RETURN:
        out = K_RETURN;
        break;
      case SDLK_ESCAPE:
        out = K_ESCAPE;
        break;
      case SDLK_BACKSPACE:
        out = K_BACKSPACE;
        break;
      case SDLK_TAB:
        out = K_TAB;
        break;
      case SDLK_CAPSLOCK:
        out = K_CAPSLOCK;
        break;
      case SDLK_F1:
        out = K_F1;
        break;
      case SDLK_F2:
        out = K_F2;
        break;
      case SDLK_F3:
        out = K_F3;
        break;
      case SDLK_F4:
        out = K_F4;
        break;
      case SDLK_F5:
        out = K_F5;
        break;
      case SDLK_F6:
        out = K_F6;
        break;
      case SDLK_F7:
        out = K_F7;
        break;
      case SDLK_F8:
        out = K_F8;
        break;
      case SDLK_F9:
        out = K_F9;
        break;
      case SDLK_F10:
        out = K_F10;
        break;
      case SDLK_F11:
        out = K_F11;
        break;
      case SDLK_F12:
        out = K_F12;
        break;
      case SDLK_PRINTSCREEN:
        out = K_PRINTSCREEN;
        break;
      case SDLK_SCROLLLOCK:
        out = K_SCROLLLOCK;
        break;
      case SDLK_PAUSE:
        out = K_PAUSE;
        break;
      case SDLK_INSERT:
        out = K_INSERT;
        break;
      case SDLK_HOME:
        out = K_HOME;
        break;
      case SDLK_PAGEUP:
        out = K_PAGEUP;
        break;
      case SDLK_DELETE:
        out = K_DELETE;
        break;
      case SDLK_END:
        out = K_END;
        break;
      case SDLK_PAGEDOWN:
        out = K_PAGEDOWN;
        break;
      case SDLK_RIGHT:
        out = K_RIGHT;
        break;
      case SDLK_LEFT:
        out = K_LEFT;
        break;
      case SDLK_DOWN:
        out = K_DOWN;
        break;
      case SDLK_UP:
        out = K_UP;
        break;
      case SDLK_NUMLOCKCLEAR:
        out = K_NUMLOCKCLEAR;
        break;
      case SDLK_KP_DIVIDE:
        out = K_KP_DIVIDE;
        break;
      case SDLK_KP_MULTIPLY:
        out = K_KP_MULTIPLY;
        break;
      case SDLK_KP_MINUS:
        out = K_KP_MINUS;
        break;
      case SDLK_KP_PLUS:
        out = K_KP_PLUS;
        break;
      case SDLK_KP_ENTER:
        out = K_KP_ENTER;
        break;
      case SDLK_KP_1:
        out = K_KP_1;
        break;
      case SDLK_KP_2:
        out = K_KP_2;
        break;
      case SDLK_KP_3:
        out = K_KP_3;
        break;
      case SDLK_KP_4:
        out = K_KP_4;
        break;
      case SDLK_KP_5:
        out = K_KP_5;
        break;
      case SDLK_KP_6:
        out = K_KP_6;
        break;
      case SDLK_KP_7:
        out = K_KP_7;
        break;
      case SDLK_KP_8:
        out = K_KP_8;
        break;
      case SDLK_KP_9:
        out = K_KP_9;
        break;
      case SDLK_KP_0:
        out = K_KP_0;
        break;
      case SDLK_KP_PERIOD:
        out = K_KP_PERIOD;
        break;
      case SDLK_APPLICATION:
        out = K_APPLICATION;
        break;
      case SDLK_POWER:
        out = K_POWER;
        break;
      case SDLK_KP_EQUALS:
        out = K_KP_EQUALS;
        break;
      case SDLK_F13:
        out = K_F13;
        break;
      case SDLK_F14:
        out = K_F14;
        break;
      case SDLK_F15:
        out = K_F15;
        break;
      case SDLK_F16:
        out = K_F16;
        break;
      case SDLK_F17:
        out = K_F17;
        break;
      case SDLK_F18:
        out = K_F18;
        break;
      case SDLK_F19:
        out = K_F19;
        break;
      case SDLK_F20:
        out = K_F20;
        break;
      case SDLK_F21:
        out = K_F21;
        break;
      case SDLK_F22:
        out = K_F22;
        break;
      case SDLK_F23:
        out = K_F23;
        break;
      case SDLK_F24:
        out = K_F24;
        break;
      case SDLK_EXECUTE:
        out = K_EXECUTE;
        break;
      case SDLK_HELP:
        out = K_HELP;
        break;
      case SDLK_MENU:
        out = K_MENU;
        break;
      case SDLK_SELECT:
        out = K_SELECT;
        break;
      case SDLK_STOP:
        out = K_STOP;
        break;
      case SDLK_AGAIN:
        out = K_AGAIN;
        break;
      case SDLK_UNDO:
        out = K_UNDO;
        break;
      case SDLK_CUT:
        out = K_CUT;
        break;
      case SDLK_COPY:
        out = K_COPY;
        break;
      case SDLK_PASTE:
        out = K_PASTE;
        break;
      case SDLK_FIND:
        out = K_FIND;
        break;
      case SDLK_MUTE:
        out = K_MUTE;
        break;
      case SDLK_VOLUMEUP:
        out = K_VOLUMEUP;
        break;
      case SDLK_VOLUMEDOWN:
        out = K_VOLUMEDOWN;
        break;
      case SDLK_KP_COMMA:
        out = K_KP_COMMA;
        break;
      case SDLK_KP_EQUALSAS400:
        out = K_KP_EQUALSAS400;
        break;
      case SDLK_ALTERASE:
        out = K_ALTERASE;
        break;
      case SDLK_SYSREQ:
        out = K_SYSREQ;
        break;
      case SDLK_CANCEL:
        out = K_CANCEL;
        break;
      case SDLK_CLEAR:
        out = K_CLEAR;
        break;
      case SDLK_PRIOR:
        out = K_PRIOR;
        break;
      case SDLK_RETURN2:
        out = K_RETURN2;
        break;
      case SDLK_SEPARATOR:
        out = K_SEPARATOR;
        break;
      case SDLK_OUT:
        out = K_OUT;
        break;
      case SDLK_OPER:
        out = K_OPER;
        break;
      case SDLK_CLEARAGAIN:
        out = K_CLEARAGAIN;
        break;
      case SDLK_CRSEL:
        out = K_CRSEL;
        break;
      case SDLK_EXSEL:
        out = K_EXSEL;
        break;
      case SDLK_KP_00:
        out = K_KP_00;
        break;
      case SDLK_KP_000:
        out = K_KP_000;
        break;
      case SDLK_THOUSANDSSEPARATOR:
        out = K_THOUSANDSSEPARATOR;
        break;
      case SDLK_DECIMALSEPARATOR:
        out = K_DECIMALSEPARATOR;
        break;
      case SDLK_CURRENCYUNIT:
        out = K_CURRENCYUNIT;
        break;
      case SDLK_CURRENCYSUBUNIT:
        out = K_CURRENCYSUBUNIT;
        break;
      case SDLK_KP_LEFTPAREN:
        out = K_KP_LEFTPAREN;
        break;
      case SDLK_KP_RIGHTPAREN:
        out = K_KP_RIGHTPAREN;
        break;
      case SDLK_KP_LEFTBRACE:
        out = K_KP_LEFTBRACE;
        break;
      case SDLK_KP_RIGHTBRACE:
        out = K_KP_RIGHTBRACE;
        break;
      case SDLK_KP_TAB:
        out = K_KP_TAB;
        break;
      case SDLK_KP_BACKSPACE:
        out = K_KP_BACKSPACE;
        break;
      case SDLK_KP_A:
        out = K_KP_A;
        break;
      case SDLK_KP_B:
        out = K_KP_B;
        break;
      case SDLK_KP_C:
        out = K_KP_C;
        break;
      case SDLK_KP_D:
        out = K_KP_D;
        break;
      case SDLK_KP_E:
        out = K_KP_E;
        break;
      case SDLK_KP_F:
        out = K_KP_F;
        break;
      case SDLK_KP_XOR:
        out = K_KP_XOR;
        break;
      case SDLK_KP_POWER:
        out = K_KP_POWER;
        break;
      case SDLK_KP_PERCENT:
        out = K_KP_PERCENT;
        break;
      case SDLK_KP_LESS:
        out = K_KP_LESS;
        break;
      case SDLK_KP_GREATER:
        out = K_KP_GREATER;
        break;
      case SDLK_KP_AMPERSAND:
        out = K_KP_AMPERSAND;
        break;
      case SDLK_KP_DBLAMPERSAND:
        out = K_KP_DBLAMPERSAND;
        break;
      case SDLK_KP_VERTICALBAR:
        out = K_KP_VERTICALBAR;
        break;
      case SDLK_KP_DBLVERTICALBAR:
        out = K_KP_DBLVERTICALBAR;
        break;
      case SDLK_KP_COLON:
        out = K_KP_COLON;
        break;
      case SDLK_KP_HASH:
        out = K_KP_HASH;
        break;
      case SDLK_KP_SPACE:
        out = K_KP_SPACE;
        break;
      case SDLK_KP_AT:
        out = K_KP_AT;
        break;
      case SDLK_KP_EXCLAM:
        out = K_KP_EXCLAM;
        break;
      case SDLK_KP_MEMSTORE:
        out = K_KP_MEMSTORE;
        break;
      case SDLK_KP_MEMRECALL:
        out = K_KP_MEMRECALL;
        break;
      case SDLK_KP_MEMCLEAR:
        out = K_KP_MEMCLEAR;
        break;
      case SDLK_KP_MEMADD:
        out = K_KP_MEMADD;
        break;
      case SDLK_KP_MEMSUBTRACT:
        out = K_KP_MEMSUBTRACT;
        break;
      case SDLK_KP_MEMMULTIPLY:
        out = K_KP_MEMMULTIPLY;
        break;
      case SDLK_KP_MEMDIVIDE:
        out = K_KP_MEMDIVIDE;
        break;
      case SDLK_KP_PLUSMINUS:
        out = K_KP_PLUSMINUS;
        break;
      case SDLK_KP_CLEAR:
        out = K_KP_CLEAR;
        break;
      case SDLK_KP_CLEARENTRY:
        out = K_KP_CLEARENTRY;
        break;
      case SDLK_KP_BINARY:
        out = K_KP_BINARY;
        break;
      case SDLK_KP_OCTAL:
        out = K_KP_OCTAL;
        break;
      case SDLK_KP_DECIMAL:
        out = K_KP_DECIMAL;
        break;
      case SDLK_KP_HEXADECIMAL:
        out = K_KP_HEXADECIMAL;
        break;
      case SDLK_LCTRL:
        out = K_LCTRL;
        break;
      case SDLK_LSHIFT:
        out = K_LSHIFT;
        break;
      case SDLK_LALT:
        out = K_LALT;
        break;
      case SDLK_LGUI:
        out = K_LGUI;
        break;
      case SDLK_RCTRL:
        out = K_RCTRL;
        break;
      case SDLK_RSHIFT:
        out = K_RSHIFT;
        break;
      case SDLK_RALT:
        out = K_RALT;
        break;
      case SDLK_RGUI:
        out = K_RGUI;
        break;
      case SDLK_MODE:
        out = K_MODE;
        break;
      case SDLK_AUDIONEXT:
        out = K_AUDIONEXT;
        break;
      case SDLK_AUDIOPREV:
        out = K_AUDIOPREV;
        break;
      case SDLK_AUDIOSTOP:
        out = K_AUDIOSTOP;
        break;
      case SDLK_AUDIOPLAY:
        out = K_AUDIOPLAY;
        break;
      case SDLK_AUDIOMUTE:
        out = K_AUDIOMUTE;
        break;
      case SDLK_MEDIASELECT:
        out = K_MEDIASELECT;
        break;
      case SDLK_WWW:
        out = K_WWW;
        break;
      case SDLK_MAIL:
        out = K_MAIL;
        break;
      case SDLK_CALCULATOR:
        out = K_CALCULATOR;
        break;
      case SDLK_COMPUTER:
        out = K_COMPUTER;
        break;
      case SDLK_AC_SEARCH:
        out = K_AC_SEARCH;
        break;
      case SDLK_AC_HOME:
        out = K_AC_HOME;
        break;
      case SDLK_AC_BACK:
        out = K_AC_BACK;
        break;
      case SDLK_AC_FORWARD:
        out = K_AC_FORWARD;
        break;
      case SDLK_AC_STOP:
        out = K_AC_STOP;
        break;
      case SDLK_AC_REFRESH:
        out = K_AC_REFRESH;
        break;
      case SDLK_AC_BOOKMARKS:
        out = K_AC_BOOKMARKS;
        break;
      case SDLK_BRIGHTNESSDOWN:
        out = K_BRIGHTNESSDOWN;
        break;
      case SDLK_BRIGHTNESSUP:
        out = K_BRIGHTNESSUP;
        break;
      case SDLK_DISPLAYSWITCH:
        out = K_DISPLAYSWITCH;
        break;
      case SDLK_KBDILLUMTOGGLE:
        out = K_KBDILLUMTOGGLE;
        break;
      case SDLK_KBDILLUMDOWN:
        out = K_KBDILLUMDOWN;
        break;
      case SDLK_KBDILLUMUP:
        out = K_KBDILLUMUP;
        break;
      case SDLK_EJECT:
        out = K_EJECT;
        break;
      case SDLK_SLEEP:
        out = K_SLEEP;
        break;
    }
  }

  if (keysym.scancode == SDL_SCANCODE_GRAVE) {
    out = K_CONSOLE;
  }

  return out;
}

void System::PumpSDLEvents() {
  SDL_Event ev;

  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_KEYDOWN: {
        Keycode keycode = TranslateSDLKey(ev.key.keysym);

        if (keycode != K_UNKNOWN) {
          QueueEvent(MakeKeyEvent(keycode, 1));
        }
      } break;

      case SDL_KEYUP: {
        Keycode keycode = TranslateSDLKey(ev.key.keysym);

        if (keycode != K_UNKNOWN) {
          QueueEvent(MakeKeyEvent(keycode, 0));
        }
      } break;

      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP: {
        Keycode keycode;

        switch (ev.button.button) {
          case SDL_BUTTON_LEFT:
            keycode = K_MOUSE1;
            break;
          case SDL_BUTTON_RIGHT:
            keycode = K_MOUSE2;
            break;
          case SDL_BUTTON_MIDDLE:
            keycode = K_MOUSE3;
            break;
          case SDL_BUTTON_X1:
            keycode = K_MOUSE4;
            break;
          case SDL_BUTTON_X2:
            keycode = K_MOUSE5;
            break;
          default:
            keycode = K_UNKNOWN;
            break;
        }

        if (keycode != K_UNKNOWN) {
          QueueEvent(
              MakeKeyEvent(keycode, ev.type == SDL_MOUSEBUTTONDOWN ? 1 : 0));
        }
      } break;

      case SDL_MOUSEWHEEL:
        if (ev.wheel.y > 0) {
          QueueEvent(MakeKeyEvent(K_MWHEELUP, 1));
          QueueEvent(MakeKeyEvent(K_MWHEELUP, 0));
        } else {
          QueueEvent(MakeKeyEvent(K_MWHEELDOWN, 1));
          QueueEvent(MakeKeyEvent(K_MWHEELDOWN, 0));
        }
        break;

      case SDL_MOUSEMOTION:
        QueueEvent(MakeMouseMoveEvent(ev.motion.x, ev.motion.y));
        break;

      case SDL_JOYDEVICEADDED:
      case SDL_JOYDEVICEREMOVED:
        InitJoystick();
        break;

      case SDL_JOYAXISMOTION:
        if (ev.jaxis.axis < NUM_JOYSTICK_AXES) {
          QueueEvent(
              MakeKeyEvent((Keycode)(K_AXIS0 + ev.jaxis.axis), ev.jaxis.value));
        }
        break;

      case SDL_JOYBUTTONDOWN:
      case SDL_JOYBUTTONUP:
        if (ev.jbutton.button < NUM_JOYSTICK_KEYS) {
          QueueEvent(MakeKeyEvent((Keycode)(K_JOY1 + ev.jbutton.button),
                                  ev.type == SDL_JOYBUTTONDOWN ? 1 : 0));
        }
        break;

      case SDL_WINDOWEVENT:
        switch (ev.window.event) {
          case SDL_WINDOWEVENT_RESIZED: {
            video_width_ = ev.window.data1;
            video_height_ = ev.window.data2;

            QueueEvent(MakeResizeEvent(video_width_, video_height_));
          } break;
        }
        break;

      case SDL_QUIT:
        exit(EXIT_SUCCESS);
        break;
    }
  }
}
