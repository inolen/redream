#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>
#include "ui/keycode.h"

#ifdef __cplusplus
extern "C" {
#endif

struct rb_s;
struct imgui_s;
struct microprofile_s;

struct SDL_Window;

static const int MAX_WINDOW_LISTENERS = 16;
static const int NUM_JOYSTICK_AXES = (K_AXIS15 - K_AXIS0) + 1;
static const int NUM_JOYSTICK_KEYS = (K_JOY31 - K_JOY0) + 1;
static const int NUM_JOYSTICK_HATS =
    ((K_HAT15 - K_HAT0) + 1) / 4;  // 4 keys per hat

typedef void (*window_prepaint_cb)(void *data);
typedef void (*window_paint_cb)(void *data, bool show_main_menu);
typedef void (*window_postpaint_cb)(void *data);
typedef void (*window_keydown_cb)(void *data, keycode_t code, int16_t value);
typedef void (*window_textinput_cb)(void *data, const char *text);
typedef void (*window_mousemove_cb)(void *data, int x, int y);
typedef void (*window_close_cb)(void *data);

typedef struct {
  window_prepaint_cb prepaint;
  window_paint_cb paint;
  window_postpaint_cb postpaint;
  window_keydown_cb keydown;
  window_textinput_cb textinput;
  window_mousemove_cb mousemove;
  window_close_cb close;
} window_callbacks_t;

struct window_s;
struct window_listener_s;

struct SDL_Window *win_handle(struct window_s *win);
struct rb_s *win_render_backend(struct window_s *win);
int win_width(struct window_s *win);
int win_height(struct window_s *win);

bool win_main_menu_enabled(struct window_s *win);
void win_enable_main_menu(struct window_s *win, bool active);

bool win_text_input_enabled(struct window_s *win);
void win_enable_text_input(struct window_s *win, bool active);

void win_pump_events(struct window_s *win);

struct window_listener_s *win_add_listener(struct window_s *win,
                                           const window_callbacks_t *cb,
                                           void *data);
void win_remove_listener(struct window_s *win,
                         struct window_listener_s *listener);

struct window_s *win_create();
void win_destroy(struct window_s *win);

#ifdef __cplusplus
}
#endif

#endif
