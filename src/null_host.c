#include <stddef.h>
#include "host.h"

/*
 * audio
 */
void audio_push(struct host *base, const int16_t *data, int num_frames) {}

/*
 * video
 */
void video_gl_make_current(struct host *host, gl_context_t ctx) {}

void video_gl_destroy_context(struct host *base, gl_context_t ctx) {}

gl_context_t video_gl_create_context_from(struct host *base,
                                          gl_context_t from) {
  return NULL;
}

gl_context_t video_gl_create_context(struct host *base) {
  return NULL;
}

int video_gl_supports_multiple_contexts(struct host *base) {
  return 0;
}

int video_height(struct host *base) {
  return 0;
}

int video_width(struct host *base) {
  return 0;
}

/*
 * input
 */
void input_poll(struct host *base) {}
