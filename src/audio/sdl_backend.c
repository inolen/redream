#include "audio/backend.h"
#include "ui/window.h"

struct audio_backend {
  struct window *window;
};

void audio_queue(void *data, int samples) {}

struct audio_backend *audio_create(struct window *window) {
  struct audio_backend *audio =
      (struct audio_backend *)calloc(1, sizeof(struct audio_backend));
  audio->window = window;

  return audio;
}

void audio_destroy(struct audio_backend *audio) {
  free(audio);
}
