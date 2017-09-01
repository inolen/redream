#ifndef HOST_H
#define HOST_H

#include "keycode.h"

struct render_backend;

typedef void (*video_created_cb)(void *, struct render_backend *);
typedef void (*video_destroyed_cb)(void *);
typedef void (*video_swapped_cb)(void *);
typedef void (*input_keydown_cb)(void *, int, enum keycode, int16_t);
typedef void (*input_mousemove_cb)(void *, int, int, int);

struct host {
  /* supplied by user to hook into host events */
  void *userdata;
  video_created_cb video_created;
  video_destroyed_cb video_destroyed;
  video_swapped_cb video_swapped;
  input_keydown_cb input_keydown;
  input_mousemove_cb input_mousemove;
};

/* audio */
void audio_push(struct host *host, const int16_t *data, int frames);

/* video */
struct render_backend *video_renderer(struct host *host);

int video_can_fullscreen(struct host *host);
int video_is_fullscreen(struct host *host);
void video_set_fullscreen(struct host *host, int fullscreen);

/* input */
int16_t input_get(struct host *host, int port, int button);

#endif
