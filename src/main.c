#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <ini.h>
#include <soundio/soundio.h>
#include "core/assert.h"
#include "core/log.h"
#include "core/option.h"
#include "core/profiler.h"
#include "core/ringbuf.h"
#include "emulator.h"
#include "host.h"
#include "sys/filesystem.h"
#include "tracer.h"

DEFINE_OPTION_INT(audio, 1, "Enable audio");
DEFINE_OPTION_INT(latency, 100, "Preferred audio latency in ms");
DEFINE_OPTION_STRING(profile, "profiles/ps4.ini", "Controller profile");
DEFINE_OPTION_INT(help, 0, "Show help");

#define AUDIO_FREQ 44100
#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480

/*
 * glfw host implementation
 */
struct glfw_host {
  struct host;

  struct GLFWwindow *win;

  struct SoundIo *soundio;
  struct SoundIoDevice *soundio_device;
  struct SoundIoOutStream *soundio_stream;
  struct ringbuf *audio_frames;

  int key_map[K_NUM_KEYS];
  unsigned char btn_state[MAPLE_NUM_PORTS][NUM_JOYSTICK_BTNS];
  unsigned char hat_state[MAPLE_NUM_PORTS][NUM_JOYSTICK_HATS];
  float axis_state[MAPLE_NUM_PORTS][NUM_JOYSTICK_AXES];
};

struct glfw_host *g_host;

/*
 * audio
 */
static int audio_read_frames(struct glfw_host *host, void *data,
                             int num_frames) {
  int available = ringbuf_available(host->audio_frames);
  int size = MIN(available, num_frames * 4);
  CHECK_EQ(size % 4, 0);

  void *read_ptr = ringbuf_read_ptr(host->audio_frames);
  memcpy(data, read_ptr, size);
  ringbuf_advance_read_ptr(host->audio_frames, size);

  return size / 4;
}

static void audio_write_frames(struct glfw_host *host, const void *data,
                               int num_frames) {
  int remaining = ringbuf_remaining(host->audio_frames);
  int size = MIN(remaining, num_frames * 4);
  CHECK_EQ(size % 4, 0);

  void *write_ptr = ringbuf_write_ptr(host->audio_frames);
  memcpy(write_ptr, data, size);
  ringbuf_advance_write_ptr(host->audio_frames, size);
}

static int audio_available_frames(struct glfw_host *host) {
  int available = ringbuf_available(host->audio_frames);
  return available / 4;
}

static int audio_buffer_low(struct glfw_host *host) {
  if (!host->soundio) {
    return 0;
  }

  int low_water_mark =
      (int)((float)AUDIO_FREQ * (host->soundio_stream->software_latency));
  return audio_available_frames(host) <= low_water_mark;
}

static void audio_write_callback(struct SoundIoOutStream *outstream,
                                 int frame_count_min, int frame_count_max) {
  struct glfw_host *host = outstream->userdata;
  const struct SoundIoChannelLayout *layout = &outstream->layout;
  struct SoundIoChannelArea *areas;
  int err;

  static uint32_t tmp[AUDIO_FREQ];
  int frames_available = audio_available_frames(host);
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
      int n = MIN(frame_count - frame, array_size(tmp));

      if (frames_available > 0) {
        /* batch read frames from ring buffer */
        n = audio_read_frames(host, tmp, n);
        frames_available -= n;
      } else {
        /* write out silence */
        memset(tmp, 0, n * sizeof(tmp[0]));
      }

      /* copy frames to output stream */
      int16_t *samples = (int16_t *)tmp;

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

static void audio_underflow_callback(struct SoundIoOutStream *outstream) {
  LOG_WARNING("audio_underflow_callback");
}

void audio_push(struct host *base, const int16_t *data, int num_frames) {
  struct glfw_host *host = (struct glfw_host *)base;

  if (!host->soundio) {
    return;
  }

  audio_write_frames(host, data, num_frames);
}

static void audio_shutdown(struct glfw_host *host) {
  if (host->soundio_stream) {
    soundio_outstream_destroy(host->soundio_stream);
  }

  if (host->soundio_device) {
    soundio_device_unref(host->soundio_device);
  }

  if (host->soundio) {
    soundio_destroy(host->soundio);
  }

  if (host->audio_frames) {
    ringbuf_destroy(host->audio_frames);
  }
}

static int audio_init(struct glfw_host *host) {
  if (!OPTION_audio) {
    return 1;
  }

  int err;

  host->audio_frames = ringbuf_create(AUDIO_FREQ * 4);

  /* connect to a soundio backend */
  {
    struct SoundIo *soundio = host->soundio = soundio_create();

    if (!soundio) {
      LOG_WARNING("Error creating soundio instance");
      return 0;
    }

    if ((err = soundio_connect(soundio))) {
      LOG_WARNING("Error connecting soundio: %s", soundio_strerror(err));
      return 0;
    }

    soundio_flush_events(soundio);
  }

  /* connect to an output device */
  {
    int default_out_device_index =
        soundio_default_output_device_index(host->soundio);

    if (default_out_device_index < 0) {
      LOG_WARNING("Error finding audio output device");
      return 0;
    }

    struct SoundIoDevice *device = host->soundio_device =
        soundio_get_output_device(host->soundio, default_out_device_index);

    if (!device) {
      LOG_WARNING("Error creating output device instance");
      return 0;
    }
  }

  /* create an output stream that matches the AICA output format
     44.1 khz, 2 channel, S16 LE */
  {
    struct SoundIoOutStream *outstream = host->soundio_stream =
        soundio_outstream_create(host->soundio_device);
    outstream->userdata = host;
    outstream->format = SoundIoFormatS16NE;
    outstream->sample_rate = AUDIO_FREQ;
    outstream->write_callback = &audio_write_callback;
    outstream->underflow_callback = &audio_underflow_callback;
    outstream->software_latency = OPTION_latency / 1000.0;

    if ((err = soundio_outstream_open(outstream))) {
      LOG_WARNING("Error opening audio device: %s", soundio_strerror(err));
      return 0;
    }

    if ((err = soundio_outstream_start(outstream))) {
      LOG_WARNING("Error starting device: %s", soundio_strerror(err));
      return 0;
    }
  }

  LOG_INFO("Audio backend created, latency %.2f",
           host->soundio_stream->software_latency);

  return 1;
}

/*
 * video
 */
static void video_context_destroyed(struct glfw_host *host) {
  if (!host->video_context_destroyed) {
    return;
  }

  host->video_context_destroyed(host->userdata);
}

static void video_context_reset(struct glfw_host *host) {
  if (!host->video_context_reset) {
    return;
  }

  host->video_context_reset(host->userdata);
}

void video_gl_make_current(struct host *host, gl_context_t ctx) {
  glfwMakeContextCurrent((GLFWwindow *)ctx);
}

void video_gl_destroy_context(struct host *base, gl_context_t ctx) {
  struct glfw_host *host = (struct glfw_host *)base;

  if (host->win == ctx) {
    /* default context, nothing to do */
  } else {
    /* shared context, destroy hidden window */
    glfwDestroyWindow((GLFWwindow *)ctx);
  }
}

gl_context_t video_gl_create_context_from(struct host *base,
                                          gl_context_t from) {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_VISIBLE, GL_FALSE);

  GLFWwindow *win =
      glfwCreateWindow(VIDEO_WIDTH, VIDEO_HEIGHT, "", NULL, (GLFWwindow *)from);
  CHECK_NOTNULL(win, "Failed to create window");

  glfwMakeContextCurrent(win);

  return (gl_context_t)win;
}

gl_context_t video_gl_create_context(struct host *base) {
  struct glfw_host *host = (struct glfw_host *)base;
  return (gl_context_t)host->win;
}

int video_gl_supports_multiple_contexts(struct host *base) {
  return 1;
}

int video_height(struct host *base) {
  return VIDEO_HEIGHT;
}

int video_width(struct host *base) {
  return VIDEO_WIDTH;
}

static void video_shutdown(struct glfw_host *host) {}

static int video_init(struct glfw_host *host) {
  return 1;
}

/*
 * input
 */
#define KEY_HAT_UP(hat) (enum keycode)(K_HAT0 + hat * 4 + 0)
#define KEY_HAT_RIGHT(hat) (enum keycode)(K_HAT0 + hat * 4 + 1)
#define KEY_HAT_DOWN(hat) (enum keycode)(K_HAT0 + hat * 4 + 2)
#define KEY_HAT_LEFT(hat) (enum keycode)(K_HAT0 + hat * 4 + 3)

static void input_handle_mouse(int port, int x, int y) {
  if (!g_host->input_mouse) {
    return;
  }

  g_host->input_mouse(g_host->userdata, x, y);
}

static void input_handle_key(int port, enum keycode key, int16_t value) {
  if (g_host->input_keyboard) {
    g_host->input_keyboard(g_host->userdata, key, value);
  }

  /* if the key is mapped to a controller button, send the controller event as
     well */
  int button = g_host->key_map[key];

  if (button != NUM_CONTROLS && g_host->input_controller) {
    g_host->input_controller(g_host->userdata, port, button, value);
  }
}

static void input_handle_hat(int port, int hat, int state, int16_t value) {
  switch (state) {
    case GLFW_HAT_UP:
      input_handle_key(port, KEY_HAT_UP(hat), value);
      break;
    case GLFW_HAT_RIGHT:
      input_handle_key(port, KEY_HAT_RIGHT(hat), value);
      break;
    case GLFW_HAT_DOWN:
      input_handle_key(port, KEY_HAT_DOWN(hat), value);
      break;
    case GLFW_HAT_LEFT:
      input_handle_key(port, KEY_HAT_LEFT(hat), value);
      break;
    case GLFW_HAT_RIGHT_UP:
      input_handle_key(port, KEY_HAT_RIGHT(hat), value);
      input_handle_key(port, KEY_HAT_UP(hat), value);
      break;
    case GLFW_HAT_RIGHT_DOWN:
      input_handle_key(port, KEY_HAT_RIGHT(hat), value);
      input_handle_key(port, KEY_HAT_DOWN(hat), value);
      break;
    case GLFW_HAT_LEFT_UP:
      input_handle_key(port, KEY_HAT_LEFT(hat), value);
      input_handle_key(port, KEY_HAT_UP(hat), value);
      break;
    case GLFW_HAT_LEFT_DOWN:
      input_handle_key(port, KEY_HAT_LEFT(hat), value);
      input_handle_key(port, KEY_HAT_DOWN(hat), value);
      break;
  }
}

static void input_update_axis(int port, int axis, float state) {
  if (g_host->axis_state[port][axis] == state) {
    return;
  }

  input_handle_key(port, K_AXIS0 + axis, (int16_t)(INT16_MAX * state));

  g_host->axis_state[port][axis] = state;
}

static void input_update_hat(int port, int hat, unsigned char state) {
  char old_state = g_host->hat_state[port][hat];

  if (old_state == state) {
    return;
  }

  /* release old hat button(s) */
  input_handle_hat(port, hat, old_state, KEY_UP);

  /* press new hat button(s) */
  input_handle_hat(port, hat, state, KEY_DOWN);

  g_host->hat_state[port][hat] = state;
}

static void input_update_button(int port, int button, unsigned char state) {
  if (g_host->btn_state[port][button] == state) {
    return;
  }

  int down = state == GLFW_PRESS || state == GLFW_REPEAT;
  int16_t value = down ? KEY_DOWN : KEY_UP;
  input_handle_key(port, K_JOY0 + button, value);

  g_host->btn_state[port][button] = state;
}

static int input_ini_handler(void *user, const char *section, const char *name,
                             const char *value) {
  struct glfw_host *host = user;

  int button = 0;
  if (!strcmp(name, "joyx")) {
    button = CONT_JOYX;
  } else if (!strcmp(name, "joyy")) {
    button = CONT_JOYY;
  } else if (!strcmp(name, "ltrig")) {
    button = CONT_LTRIG;
  } else if (!strcmp(name, "rtrig")) {
    button = CONT_RTRIG;
  } else if (!strcmp(name, "start")) {
    button = CONT_START;
  } else if (!strcmp(name, "a")) {
    button = CONT_A;
  } else if (!strcmp(name, "b")) {
    button = CONT_B;
  } else if (!strcmp(name, "x")) {
    button = CONT_X;
  } else if (!strcmp(name, "y")) {
    button = CONT_Y;
  } else if (!strcmp(name, "dpad_up")) {
    button = CONT_DPAD_UP;
  } else if (!strcmp(name, "dpad_down")) {
    button = CONT_DPAD_DOWN;
  } else if (!strcmp(name, "dpad_left")) {
    button = CONT_DPAD_LEFT;
  } else if (!strcmp(name, "dpad_right")) {
    button = CONT_DPAD_RIGHT;
  } else {
    LOG_WARNING("Unknown button %s", name);
    return 0;
  }

  enum keycode key = get_key_by_name(value);

  if (key == K_UNKNOWN) {
    LOG_WARNING("Unknown key %s", value);
    return 0;
  }

  host->key_map[key] = button;

  return 1;
}

static void input_load_profile(struct glfw_host *host, const char *path) {
  if (!*path) {
    return;
  }

  LOG_INFO("Loading controller profile %s", path);

  if (ini_parse(path, input_ini_handler, host) < 0) {
    LOG_WARNING("Failed to parse %s", path);
    return;
  }
}

void input_poll(struct host *base) {
  struct glfw_host *host = (struct glfw_host *)base;

  glfwPollEvents();

  /* query latest joystick state */
  for (int i = GLFW_JOYSTICK_1; i < GLFW_JOYSTICK_LAST; i++) {
    int present = glfwJoystickPresent(i);

    if (!present) {
      continue;
    }

    int num_buttons = 0;
    int num_hats = 0;
    int num_axes = 0;

    const unsigned char *buttons = glfwGetJoystickButtons(i, &num_buttons);
    const unsigned char *hats = glfwGetJoystickHats(i, &num_hats);
    const float *axes = glfwGetJoystickAxes(i, &num_axes);

    for (int j = 0; j < num_buttons; j++) {
      input_update_button(i, j, buttons[j]);
    }

    for (int j = 0; j < num_hats; j++) {
      input_update_hat(i, j, hats[j]);
    }

    for (int j = 0; j < num_axes; j++) {
      input_update_axis(i, j, (int16_t)(INT16_MAX * axes[j]));
    }
  }
}

static void input_shutdown(struct glfw_host *host) {}

static int input_init(struct glfw_host *host) {
  /* setup default profile */
  for (int i = 0; i < K_NUM_KEYS; i++) {
    host->key_map[i] = NUM_CONTROLS;
  }

  host->key_map[K_SPACE] = CONT_START;
  host->key_map['K'] = CONT_A;
  host->key_map['L'] = CONT_B;
  host->key_map['J'] = CONT_X;
  host->key_map['I'] = CONT_Y;
  host->key_map['W'] = CONT_DPAD_UP;
  host->key_map['S'] = CONT_DPAD_DOWN;
  host->key_map['A'] = CONT_DPAD_LEFT;
  host->key_map['D'] = CONT_DPAD_RIGHT;
  host->key_map['O'] = CONT_LTRIG;
  host->key_map['P'] = CONT_RTRIG;

  input_load_profile(host, OPTION_profile);

  return 1;
}

void host_destroy(struct glfw_host *host) {
  input_shutdown(host);

  video_shutdown(host);

  audio_shutdown(host);

  free(host);
}

struct glfw_host *host_create(struct GLFWwindow *win) {
  struct glfw_host *host = calloc(1, sizeof(struct glfw_host));

  host->win = win;

  if (!audio_init(host)) {
    host_destroy(host);
    return NULL;
  }

  if (!video_init(host)) {
    host_destroy(host);
    return NULL;
  }

  if (!input_init(host)) {
    host_destroy(host);
    return NULL;
  }

  return host;
}

/*
 * core
 */
static void mouse_button_callback(GLFWwindow *win, int button, int action,
                                  int mods) {
  enum keycode key = K_UNKNOWN;

  switch (button) {
    case GLFW_MOUSE_BUTTON_LEFT:
      key = K_MOUSE1;
      break;
    case GLFW_MOUSE_BUTTON_RIGHT:
      key = K_MOUSE2;
      break;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      key = K_MOUSE3;
      break;
  }

  if (key == K_UNKNOWN) {
    return;
  }

  int down = action == GLFW_PRESS || action == GLFW_REPEAT;
  int16_t value = down ? KEY_DOWN : KEY_UP;
  input_handle_key(0, key, value);
}

static void mouse_move_callback(GLFWwindow *win, double x, double y) {
  input_handle_mouse(0, (int)x, (int)y);
}

static void key_callback(GLFWwindow *win, int key, int scancode, int action,
                         int mods) {
  int down = action == GLFW_PRESS || action == GLFW_REPEAT;
  int16_t value = down ? KEY_DOWN : KEY_UP;
  input_handle_key(0, key, value);
}

int main(int argc, char **argv) {
  const char *appdir = fs_appdir();
  if (!fs_mkdir(appdir)) {
    LOG_FATAL("Failed to create app directory %s", appdir);
  }

  /* load base options from config */
  char config[PATH_MAX] = {0};
  snprintf(config, sizeof(config), "%s" PATH_SEPARATOR "config", appdir);
  options_read(config);

  /* override options from the command line */
  options_parse(&argc, &argv);

  if (OPTION_help) {
    options_print_help();
    return EXIT_SUCCESS;
  }

  /* init glfw window and context */
  int res = glfwInit();
  CHECK(res, "Failed to initialize GLFW");

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  GLFWwindow *win =
      glfwCreateWindow(VIDEO_WIDTH, VIDEO_HEIGHT, "redream", NULL, NULL);
  CHECK_NOTNULL(win, "Failed to create window");

  glfwMakeContextCurrent(win);
  glfwSwapInterval(0);

  /* bind window input callbacks */
  glfwSetKeyCallback(win, key_callback);
  glfwSetCursorPosCallback(win, mouse_move_callback);
  glfwSetMouseButtonCallback(win, mouse_button_callback);

  /* link in gl functions at runtime */
  glewExperimental = GL_TRUE;
  GLenum err = glewInit();
  CHECK_EQ(err, GLEW_OK, "GLEW initialization failed: %s",
           glewGetErrorString(err));

  /* init host audio, video and input systems */
  g_host = host_create(win);
  if (!g_host) {
    return EXIT_FAILURE;
  }

  const char *load = argc > 1 ? argv[1] : NULL;

  if (load && strstr(load, ".trace")) {
    struct tracer *tracer = tracer_create((struct host *)g_host);

    if (tracer_load(tracer, load)) {
      while (1) {
        if (glfwWindowShouldClose(win)) {
          break;
        }

        glfwPollEvents();

        tracer_run(tracer);

        glfwSwapBuffers(win);
      }
    }

    tracer_destroy(tracer);
  } else {
    struct emu *emu = emu_create((struct host *)g_host);

    /* tell the emulator a valid video context has been aquired */
    video_context_reset(g_host);

    if (emu_load_game(emu, load)) {
      while (1) {
        if (glfwWindowShouldClose(win)) {
          break;
        }

        /* even though the emulator itself will poll for events when updating
           controller input, the main loop needs to also poll to ensure the
           close event is received */
        glfwPollEvents();

        /* run a frame if the available audio is running low. this synchronizes
           the emulation speed with the host audio clock. note however, if audio
           is disabled, the emulator will run as fast as possible */
        if (!OPTION_audio || audio_buffer_low(g_host)) {
          emu_run(emu);
        }

        glfwSwapBuffers(win);
      }
    }

    video_context_destroyed(g_host);

    emu_destroy(emu);
  }

  host_destroy(g_host);

  /* destroy glfw */
  glfwDestroyWindow(win);
  glfwTerminate();

  /* persist options for next run */
  options_write(config);

  return EXIT_SUCCESS;
}
