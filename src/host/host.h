#ifndef HOST_H
#define HOST_H

struct host;

/* audio */
void audio_push(struct host *host, const int16_t *data, int frames);

/* video */
int video_can_fullscreen(struct host *host);
int video_is_fullscreen(struct host *host);
void video_set_fullscreen(struct host *host, int fullscreen);

/* input */
int16_t input_get(struct host *host, int port, int button);

#endif
