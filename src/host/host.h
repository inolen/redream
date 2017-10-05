#ifndef HOST_H
#define HOST_H

struct host;

/* audio */
void audio_push(struct host *host, const int16_t *data, int frames);

#endif
