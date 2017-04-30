#ifndef AUDIO_BACKEND_H
#define AUDIO_BACKEND_H

struct audio_backend;
struct ringbuf;

struct audio_backend *audio_create(struct ringbuf *buffer);
void audio_destroy(struct audio_backend *audio);

int audio_buffer_low(struct audio_backend *audio);
void audio_pump_events(struct audio_backend *audio);

#endif
