#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdbool.h>
#include <stdint.h>
#include "core/list.h"
#include "ui/keycode.h"

struct audio_backend;
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

#define NUM_JOYSTICK_AXES ((K_AXIS15 - K_AXIS0) + 1)
#define NUM_JOYSTICK_KEYS ((K_JOY31 - K_JOY0) + 1)
#define NUM_JOYSTICK_HATS (((K_HAT15 - K_HAT0) + 1) / 4) /* 4 keys per hat */

struct window_listener {
  void *data;
  void (*paint)(void *data);
  void (*debug_menu)(void *data, struct nk_context *ctx);
  void (*keydown)(void *data, int device_index, enum keycode code,
                  int16_t value);
  void (*textinput)(void *data, const char *text);
  void (*mousemove)(void *data, int x, int y);
  void (*close)(void *data);
  struct list_node it;
};

struct window {
  /* public */
  struct SDL_Window *handle;
  struct audio_backend *audio;
  struct render_backend *rb;
  struct nuklear *nk;
  struct microprofile *mp;

  /* read only */
  int width;
  int height;
  int fullscreen;
  bool debug_menu;
  bool text_input;

  /* private state */
  struct list listeners;
  char status[256];
  struct _SDL_Joystick *joysticks[MAX_JOYSTICKS];
  uint8_t hat_state[MAX_JOYSTICKS][NUM_JOYSTICK_HATS];
};

struct window *win_create();
void win_destroy(struct window *win);

void win_add_listener(struct window *win, struct window_listener *listener);
void win_remove_listener(struct window *win, struct window_listener *listener);

void win_pump_events(struct window *win);

void win_set_status(struct window *win, const char *status);
void win_enable_debug_menu(struct window *win, bool active);
void win_enable_text_input(struct window *win, bool active);

#endif
