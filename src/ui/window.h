#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include "ui/keycode.h"

struct rb;
struct imgui;
struct microprofile;

struct SDL_Window;

static const int MAX_WINDOW_LISTENERS = 16;
static const int NUM_JOYSTICK_AXES = (K_AXIS15 - K_AXIS0) + 1;
static const int NUM_JOYSTICK_KEYS = (K_JOY31 - K_JOY0) + 1;
static const int NUM_JOYSTICK_HATS =
    ((K_HAT15 - K_HAT0) + 1) / 4;  // 4 keys per hat

struct window_callbacks {
  void (*prepaint)(void *data);
  void (*paint)(void *data, bool show_main_menu);
  void (*postpaint)(void *data);
  void (*keydown)(void *data, enum keycode code, int16_t value);
  void (*textinput)(void *data, const char *text);
  void (*mousemove)(void *data, int x, int y);
  void (*close)(void *data);
};

struct window;
struct window_listener;

struct SDL_Window *win_handle(struct window *win);
struct rb *win_render_backend(struct window *win);
int win_width(struct window *win);
int win_height(struct window *win);

bool win_main_menu_enabled(struct window *win);
void win_enable_main_menu(struct window *win, bool active);

bool win_text_input_enabled(struct window *win);
void win_enable_text_input(struct window *win, bool active);

void win_pump_events(struct window *win);

struct window_listener *win_add_listener(struct window *win,
                                         const struct window_callbacks *cb,
                                         void *data);
void win_remove_listener(struct window *win, struct window_listener *listener);

struct window *win_create();
void win_destroy(struct window *win);

#endif
