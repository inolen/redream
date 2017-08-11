#include <stddef.h>
#include "host/host.h"

/*
 * audio
 */
void audio_push(struct host *base, const int16_t *data, int num_frames) {}

/*
 * video
 */
void video_unbind_context(struct host *base) {}

void video_bind_context(struct host *base, struct render_backend *r) {}

void video_destroy_renderer(struct host *base, struct render_backend *r) {}

struct render_backend *video_create_renderer_from(struct host *base,
                                                  struct render_backend *from) {
  return NULL;
}

struct render_backend *video_create_renderer(struct host *base) {
  return NULL;
}

int video_supports_multiple_contexts(struct host *base) {
  return 0;
}

int video_height(struct host *base) {
  return 0;
}

int video_width(struct host *base) {
  return 0;
}

void video_toggle_fullscreen(void){
  return;
}

/*
 * input
 */
void input_poll(struct host *base) {}
