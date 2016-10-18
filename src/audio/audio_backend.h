#ifndef AUDIO_BACKEND_H
#define AUDIO_BACKEND_H

struct aica;
struct audio_backend;

struct audio_backend *audio_create(struct aica *aica);
void audio_destroy(struct audio_backend *audio);

void audio_pump_events(struct audio_backend *audio);

#endif
