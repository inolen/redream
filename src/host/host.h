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
int video_can_fullscreen(struct host *host);
int video_is_fullscreen(struct host *host);
void video_set_fullscreen(struct host *host, int fullscreen);

#define on_video_created(host, r)             \
  {                                           \
    if (host->video_created) {                \
      host->video_created(host->userdata, r); \
    }                                         \
  }

#define on_video_destroyed(host)             \
  {                                          \
    if (host->video_destroyed) {             \
      host->video_destroyed(host->userdata); \
    }                                        \
  }

#define on_video_swapped(host)             \
  {                                        \
    if (host->video_swapped) {             \
      host->video_swapped(host->userdata); \
    }                                      \
  }

/* input */
int16_t input_get(struct host *host, int port, int button);

#define on_input_keydown(host, port, key, value)             \
  {                                                          \
    if (host->input_keydown) {                               \
      host->input_keydown(host->userdata, port, key, value); \
    }                                                        \
  }

#define on_input_mousemove(host, port, x, y)             \
  {                                                      \
    if (host->input_mousemove) {                         \
      host->input_mousemove(host->userdata, port, x, y); \
    }                                                    \
  }

#endif
