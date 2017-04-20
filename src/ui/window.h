#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include "core/list.h"
#include "ui/keycode.h"

#define DEBUG_MENU_HEIGHT 23.0f

struct window;
typedef void *glcontext_t;

struct window_listener {
  void *data;
  void (*joy_add)(void *data, int joystick_index);
  void (*joy_remove)(void *data, int joystick_index);
  void (*keydown)(void *data, int device_index, enum keycode code,
                  int16_t value);
  void (*mousemove)(void *data, int x, int y);
  void (*close)(void *data);
  struct list_node it;
};

struct window *win_create();
void win_destroy(struct window *win);

glcontext_t win_gl_create_context(struct window *win);
glcontext_t win_gl_create_context_from(struct window *win, glcontext_t other);
void win_gl_make_current(struct window *win, glcontext_t ctx);
void win_gl_swap_buffers(struct window *win);
void win_gl_destroy_context(struct window *win, glcontext_t ctx);

void win_add_listener(struct window *win, struct window_listener *listener);
void win_remove_listener(struct window *win, struct window_listener *listener);

int win_width(struct window *win);
int win_height(struct window *win);

int win_fullscreen(struct window *win);
void win_set_fullscreen(struct window *win, int fullscreen);

void win_pump_events(struct window *win);

#endif
