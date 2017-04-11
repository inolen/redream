#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include "core/list.h"
#include "ui/keycode.h"

struct imgui;
struct microprofile;
struct nuklear;
struct nk_context;
struct render_backend;
struct window;
struct window_listener;

struct _SDL_Joystick;
struct SDL_Window;

#define MAX_WINDOW_LISTENERS 8

#define DEBUG_MENU_HEIGHT 23.0f

#define MAX_JOYSTICKS 4

#define NUM_JOYSTICK_AXES ((K_AXIS25 - K_AXIS0) + 1)
#define NUM_JOYSTICK_KEYS ((K_JOY31 - K_JOY0) + 1)
#define NUM_JOYSTICK_HATS (((K_HAT15 - K_HAT0) + 1) / 4) /* 4 keys per hat */

typedef void *glcontext_t;

struct window_listener {
  void *data;
  void (*paint)(void *data);
  void (*joy_add)(void *data, int joystick_index);
  void (*joy_remove)(void *data, int joystick_index);
  void (*keydown)(void *data, int device_index, enum keycode code,
                  int16_t value);
  void (*mousemove)(void *data, int x, int y);
  void (*close)(void *data);
  struct list_node it;
};

struct window {
  /* public */
  struct SDL_Window *handle;

  /* read only */
  int width;
  int height;
  int fullscreen;

  /* private state */
  struct list listeners;
  char status[256];
  struct _SDL_Joystick *joysticks[MAX_JOYSTICKS];
  uint8_t hat_state[MAX_JOYSTICKS][NUM_JOYSTICK_HATS];
};

struct window *win_create();
void win_destroy(struct window *win);

glcontext_t win_gl_create_context(struct window *win);
void win_gl_make_current(struct window *win, glcontext_t ctx);
void win_gl_destroy_context(struct window *win, glcontext_t ctx);

void win_add_listener(struct window *win, struct window_listener *listener);
void win_remove_listener(struct window *win, struct window_listener *listener);

void win_set_fullscreen(struct window *win, int fullscreen);

void win_pump_events(struct window *win);

#endif
