#ifndef AUDIO_BACKEND_H
#define AUDIO_BACKEND_H

struct audio_backend;
struct window;

void audio_queue(void *data, int samples);

struct audio_backend *audio_create(struct window *window);
void audio_destroy(struct audio_backend *audio);

#endif
