#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>
#include <stdint.h>
#include "core/list.h"
#include "ui/keycode.h"

struct rb;
struct imgui;
struct microprofile;
struct nuklear;
struct nk_context;
struct window;
struct window_listener;

struct _SDL_Joystick;
struct SDL_Window;

#define MAX_WINDOW_LISTENERS 16
#define NUM_JOYSTICK_AXES ((K_AXIS15 - K_AXIS0) + 1)
#define NUM_JOYSTICK_KEYS ((K_JOY31 - K_JOY0) + 1)
#define NUM_JOYSTICK_HATS (((K_HAT15 - K_HAT0) + 1) / 4) /* 4 keys per hat */

struct window_callbacks {
  void (*paint)(void *data);
  void (*paint_menubar)(void *data, struct nk_context *ctx);
  void (*paint_ui)(void *data, struct nk_context *ctx);
  void (*keydown)(void *data, enum keycode code, int16_t value);
  void (*textinput)(void *data, const char *text);
  void (*mousemove)(void *data, int x, int y);
  void (*close)(void *data);
};

struct window_listener {
  struct window_callbacks cb;
  void *data;
  struct list_node it;
};

struct window {
  // public
  struct SDL_Window *handle;
  struct rb *rb;
  struct nuklear *nk;
  struct microprofile *mp;

  // read only
  int width;
  int height;
  bool menubar;
  bool text_input;

  // private state
  struct window_listener listeners[MAX_WINDOW_LISTENERS];
  struct list free_listeners;
  struct list live_listeners;

  struct _SDL_Joystick *joystick;
  uint8_t hat_state[NUM_JOYSTICK_HATS];
};

void win_enable_menubar(struct window *win, bool active);
void win_enable_text_input(struct window *win, bool active);
void win_pump_events(struct window *win);
struct window_listener *win_add_listener(struct window *win,
                                         const struct window_callbacks *cb,
                                         void *data);
void win_remove_listener(struct window *win, struct window_listener *listener);
struct window *win_create();
void win_destroy(struct window *win);

#endif
