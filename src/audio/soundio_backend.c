#include <soundio/soundio.h>
#include "audio/audio_backend.h"
#include "hw/aica/aica.h"

DEFINE_OPTION_INT(latency, 100, "Set preferred audio latency in MS");

struct audio_backend {
  struct aica *aica;
  struct SoundIo *soundio;
  struct SoundIoDevice *device;
  struct SoundIoOutStream *outstream;
  uint32_t frames[AICA_SAMPLE_FREQ];
};

static void audio_write_callback(struct SoundIoOutStream *outstream,
                                 int frame_count_min, int frame_count_max) {
  struct audio_backend *audio = outstream->userdata;
  const struct SoundIoChannelLayout *layout = &outstream->layout;
  struct SoundIoChannelArea *areas;
  int err;

  int16_t *samples = (int16_t *)audio->frames;
  int frames_available = aica_available_frames(audio->aica);
  int frames_silence = frame_count_max - frames_available;
  int frames_remaining = frames_available + frames_silence;

  while (frames_remaining > 0) {
    int frame_count = frames_remaining;

    if ((err =
             soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
      LOG_WARNING("Error writing to output stream: %s", soundio_strerror(err));
      break;
    }

    if (!frame_count) {
      break;
    }

    for (int frame = 0; frame < frame_count;) {
      int n = MIN(frame_count - frame, array_size(audio->frames));

      if (frames_available > 0) {
        /* batch read frames from aica */
        n = aica_read_frames(audio->aica, audio->frames, n);
        frames_available -= n;
      } else {
        /* write out silence */
        memset(audio->frames, 0, n * 4);
      }

      /* copy frames to output stream */
      for (int channel = 0; channel < layout->channel_count; channel++) {
        struct SoundIoChannelArea *area = &areas[channel];

        for (int i = 0; i < n; i++) {
          int16_t *ptr = (int16_t *)(area->ptr + area->step * (frame + i));
          *ptr = samples[channel + 2 * i];
        }
      }

      frame += n;
    }

    if ((err = soundio_outstream_end_write(outstream))) {
      LOG_WARNING("Error writing to output stream: %s", soundio_strerror(err));
      break;
    }

    frames_remaining -= frame_count;
  }
}

void audio_underflow_callback(struct SoundIoOutStream *outstream) {
  LOG_WARNING("audio_underflow_callback");
}

void audio_pump_events(struct audio_backend *audio) {
  soundio_flush_events(audio->soundio);
}

int audio_buffer_low(struct audio_backend *audio) {
  int low_water_mark = (int)((float)AICA_SAMPLE_FREQ * (OPTION_latency / 1000.0f));
  return aica_available_frames(audio->aica) <= low_water_mark;
}

void audio_destroy(struct audio_backend *audio) {
  if (audio->outstream) {
    soundio_outstream_destroy(audio->outstream);
  }

  if (audio->device) {
    soundio_device_unref(audio->device);
  }

  if (audio->soundio) {
    soundio_destroy(audio->soundio);
  }

  free(audio);
}

struct audio_backend *audio_create(struct aica *aica) {
  int err;

  struct audio_backend *audio = calloc(1, sizeof(struct audio_backend));
  audio->aica = aica;

  /* connect to a soundio backend */
  {
    audio->soundio = soundio_create();

    if (!audio->soundio) {
      LOG_WARNING("Error creating soundio instance");
      audio_destroy(audio);
      return NULL;
    }

    if ((err = soundio_connect(audio->soundio))) {
      LOG_WARNING("Error connecting soundio: %s", soundio_strerror(err));
      audio_destroy(audio);
      return NULL;
    }

    soundio_flush_events(audio->soundio);
  }

  /* connect to an output device */
  {
    int default_out_device_index =
        soundio_default_output_device_index(audio->soundio);

    if (default_out_device_index < 0) {
      LOG_WARNING("Error finding audio output device");
      audio_destroy(audio);
      return NULL;
    }

    audio->device =
        soundio_get_output_device(audio->soundio, default_out_device_index);

    if (!audio->device) {
      LOG_WARNING("Error creating output device instance");
      audio_destroy(audio);
      return NULL;
    }
  }

  /* create an output stream that matches the AICA output format
     44.1 khz, 2 channel, S16 LE */
  {
    audio->outstream = soundio_outstream_create(audio->device);
    audio->outstream->format = SoundIoFormatS16NE;
    audio->outstream->sample_rate = AICA_SAMPLE_FREQ;
    audio->outstream->userdata = audio;
    audio->outstream->write_callback = &audio_write_callback;
    audio->outstream->underflow_callback = &audio_underflow_callback;
    audio->outstream->software_latency = OPTION_latency / 1000.0;

    if ((err = soundio_outstream_open(audio->outstream))) {
      LOG_WARNING("Error opening audio device: %s", soundio_strerror(err));
      audio_destroy(audio);
      return NULL;
    }

    if ((err = soundio_outstream_start(audio->outstream))) {
      LOG_WARNING("Error starting device: %s", soundio_strerror(err));
      audio_destroy(audio);
      return NULL;
    }
  }

  LOG_INFO("Audio backend created, latency %.2f",
           audio->outstream->software_latency);

  return audio;
}
