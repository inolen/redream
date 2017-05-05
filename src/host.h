#ifndef HOST_H
#define HOST_H

#include "hw/maple/maple_types.h"
#include "keycode.h"

typedef void (*video_context_reset_cb)(void *);
typedef void (*video_context_destroyed_cb)(void *);
typedef void (*input_keyboard_cb)(void *, enum keycode, int16_t);
typedef void (*input_mouse_cb)(void *, int, int);
typedef void (*input_controller_cb)(void *, int, int, int16_t);

typedef void *gl_context_t;

struct host {
  /* supplied by user to hook into host events */
  void *userdata;
  video_context_reset_cb video_context_reset;
  video_context_destroyed_cb video_context_destroyed;
  input_keyboard_cb input_keyboard;
  input_mouse_cb input_mouse;
  input_controller_cb input_controller;
};

/* audio */
void audio_push(struct host *host, const int16_t *data, int frames);

/* video */
int video_width(struct host *host);
int video_height(struct host *host);

int video_gl_supports_multiple_contexts(struct host *host);
gl_context_t video_gl_create_context(struct host *host);
gl_context_t video_gl_create_context_from(struct host *host, gl_context_t ctx);
void video_gl_destroy_context(struct host *host, gl_context_t ctx);
void video_gl_make_current(struct host *host, gl_context_t ctx);

/* input */
void input_poll(struct host *host);
int16_t input_get(struct host *host, int port, int button);

#endif
