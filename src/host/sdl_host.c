#include <glad/glad.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include "core/assert.h"
#include "core/filesystem.h"
#include "core/log.h"
#include "core/option.h"
#include "core/ringbuf.h"
#include "core/time.h"
#include "emulator.h"
#include "host/host.h"
#include "render/render_backend.h"
#include "tracer.h"

DEFINE_OPTION_INT(audio, 1, "Enable audio");
DEFINE_OPTION_INT(latency, 50, "Preferred audio latency in ms");

#define AUDIO_FREQ 44100
#define VIDEO_DEFAULT_WIDTH 640
#define VIDEO_DEFAULT_HEIGHT 480
#define INPUT_MAX_CONTROLLERS 4

#define AUDIO_FRAMES_TO_MS(frames) \
  (int)(((float)frames * 1000.0f) / (float)AUDIO_FREQ)
#define MS_TO_AUDIO_FRAMES(ms) (int)(((float)(ms) / 1000.0f) * AUDIO_FREQ)
#define NS_TO_AUDIO_FRAMES(ns) (int)(((float)(ns) / NS_PER_SEC) * AUDIO_FREQ)

/*
 * sdl host implementation
 */
struct sdl_host {
  struct host;

  struct SDL_Window *win;

  int closed;
  int video_width;
  int video_height;

  SDL_AudioDeviceID audio_dev;
  SDL_AudioSpec audio_spec;
  struct ringbuf *audio_frames;
  volatile int64_t audio_last_callback;

  int key_map[K_NUM_KEYS];
  SDL_GameController *controllers[INPUT_MAX_CONTROLLERS];
};

static void host_poll_events(struct sdl_host *host);

struct sdl_host *g_host;

/*
 * audio
 */
static int audio_read_frames(struct sdl_host *host, void *data,
                             int num_frames) {
  int available = ringbuf_available(host->audio_frames);
  int size = MIN(available, num_frames * 4);
  CHECK_EQ(size % 4, 0);

  void *read_ptr = ringbuf_read_ptr(host->audio_frames);
  memcpy(data, read_ptr, size);
  ringbuf_advance_read_ptr(host->audio_frames, size);

  return size / 4;
}

static void audio_write_frames(struct sdl_host *host, const void *data,
                               int num_frames) {
  int remaining = ringbuf_remaining(host->audio_frames);
  int size = MIN(remaining, num_frames * 4);
  CHECK_EQ(size % 4, 0);

  void *write_ptr = ringbuf_write_ptr(host->audio_frames);
  memcpy(write_ptr, data, size);
  ringbuf_advance_write_ptr(host->audio_frames, size);
}

static int audio_available_frames(struct sdl_host *host) {
  int available = ringbuf_available(host->audio_frames);
  return available / 4;
}

static int audio_buffer_low(struct sdl_host *host) {
  if (!host->audio_dev) {
    /* lie and say the audio buffer is low, forcing the emulator to run as fast
       as possible */
    return 1;
  }

  /* SDL's write callback is called very coarsely, seemingly, only each time
     its buffered data has completely drained

     since the main loop is designed to synchronize speed based on the amount
     of buffered audio data, with larger buffer sizes (due to a larger latency
     setting) this can result in the callback being called only one time for
     multiple video frames

     this creates a situation where multiple video frames are immediately ran
     when the callback fires in order to push enough audio data to avoid an
     underflow, and then multiple vblanks occur on the host where no new frame
     is presented as the main loop again blocks waiting for another write
     callback to decrease the amount of buffered audio data

     in order to smooth out the video frame timings when the audio latency is
     high, the host clock is used to interpolate the amount of available audio
     data between callbacks */
  int64_t now = time_nanoseconds();
  int64_t since_last_callback = now - host->audio_last_callback;
  int frames_available = audio_available_frames(host);
  frames_available -= NS_TO_AUDIO_FRAMES(since_last_callback);

  int low_water_mark = host->audio_spec.samples;
  return frames_available <= low_water_mark;
}

static void audio_write_callback(void *userdata, Uint8 *stream, int len) {
  static const int frame_size = 2 * 2;
  struct sdl_host *host = userdata;
  Sint32 *buf = (Sint32 *)stream;
  int frame_count_max = len / frame_size;

  static uint32_t tmp[AUDIO_FREQ];
  int frames_available = audio_available_frames(host);
  int frames_remaining = MIN(frames_available, frame_count_max);

  while (frames_remaining > 0) {
    /* batch read frames from ring buffer */
    int n = MIN(frames_remaining, array_size(tmp));
    n = audio_read_frames(host, tmp, n);
    frames_remaining -= n;

    /* copy frames to output stream */
    memcpy(buf, tmp, n * frame_size);
  }

  host->audio_last_callback = time_nanoseconds();
}

void audio_push(struct host *base, const int16_t *data, int num_frames) {
  struct sdl_host *host = (struct sdl_host *)base;

  if (!host->audio_dev) {
    return;
  }

  audio_write_frames(host, data, num_frames);
}

static void audio_shutdown(struct sdl_host *host) {
  if (host->audio_dev) {
    SDL_CloseAudioDevice(host->audio_dev);
  }

  if (host->audio_frames) {
    ringbuf_destroy(host->audio_frames);
  }
}

static int audio_init(struct sdl_host *host) {
  if (!OPTION_audio) {
    return 1;
  }

  /* match AICA output format */
  SDL_AudioSpec want;
  SDL_zero(want);
  want.freq = AUDIO_FREQ;
  want.format = AUDIO_S16LSB;
  want.channels = 2;
  want.samples = MS_TO_AUDIO_FRAMES(OPTION_latency);
  want.userdata = host;
  want.callback = audio_write_callback;

  host->audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &host->audio_spec, 0);
  if (!host->audio_dev) {
    LOG_WARNING("failed to open SDL audio device: %s", SDL_GetError());
    return 0;
  }

  /* create ringbuffer to store data coming in from AICA. note, the buffer needs
     to be at least two video frames in size, in order to handle the coarse
     synchronization used by the main loop, where an entire guest video frame is
     ran when the available audio data is deemed low */
  static const int frame_size = 2 * 2;
  host->audio_frames = ringbuf_create(AUDIO_FREQ * frame_size);

  /* resume device */
  SDL_PauseAudioDevice(host->audio_dev, 0);

  LOG_INFO("audio backend created, %d ms / %d frames latency",
           AUDIO_FRAMES_TO_MS(host->audio_spec.samples),
           host->audio_spec.samples);

  return 1;
}

/*
 * video
 */
static void video_context_destroyed(struct sdl_host *host) {
  if (!host->video_context_destroyed) {
    return;
  }

  host->video_context_destroyed(host->userdata);
}

static void video_context_reset(struct sdl_host *host) {
  if (!host->video_context_reset) {
    return;
  }

  host->video_context_reset(host->userdata);
}

static void video_resized(struct sdl_host *host) {
  if (!host->video_resized) {
    return;
  }

  host->video_resized(host->userdata);
}

static void video_gl_destroy_context(struct host *base, video_context_t ctx) {
  struct sdl_host *host = (struct sdl_host *)base;

  /* make sure the context is no longer active */
  int res = SDL_GL_MakeCurrent(host->win, NULL);
  CHECK_EQ(res, 0);

  SDL_GL_DeleteContext(ctx);
}

static video_context_t video_gl_create_context(struct sdl_host *host) {
#if PLATFORM_ANDROID
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

  /* SDL defaults to allocating a 16-bit depth buffer, raise this to at least
     24-bits to help with the depth precision lost when converting from PVR
     coordinates to OpenGL */
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  SDL_GLContext ctx = SDL_GL_CreateContext(host->win);
  CHECK_NOTNULL(ctx, "video_gl_create_context failed: %s", SDL_GetError());

  /* disable vsync */
  int res = SDL_GL_SetSwapInterval(0);
  CHECK_EQ(res, 0, "video_gl_create_context failed to disable vsync");

  /* link in gl functions at runtime */
  res = gladLoadGLLoader((GLADloadproc)&SDL_GL_GetProcAddress);
  CHECK_EQ(res, 1, "video_gl_create_context failed to link");

  return (video_context_t)ctx;
}

void video_destroy_renderer(struct host *base, struct render_backend *r) {
  video_context_t ctx = r_context(r);
  r_destroy(r);
  video_gl_destroy_context(base, ctx);
}

struct render_backend *video_create_renderer(struct host *base) {
  struct sdl_host *host = (struct sdl_host *)base;
  video_context_t ctx = video_gl_create_context(host);
  return r_create(ctx);
}

int video_height(struct host *base) {
  struct sdl_host *host = (struct sdl_host *)base;
  return host->video_height;
}

int video_width(struct host *base) {
  struct sdl_host *host = (struct sdl_host *)base;
  return host->video_width;
}

static void video_shutdown(struct sdl_host *host) {}

static int video_init(struct sdl_host *host) {
  return 1;
}

/*
 * input
 */
static enum keycode translate_sdl_key(SDL_Keysym keysym) {
  enum keycode out = K_UNKNOWN;

  if (keysym.sym >= SDLK_SPACE && keysym.sym <= SDLK_z) {
    /* this range maps 1:1 with ASCII chars */
    out = (enum keycode)keysym.sym;
  } else {
    switch (keysym.sym) {
      case SDLK_CAPSLOCK:
        out = K_CAPSLOCK;
        break;
      case SDLK_RETURN:
        out = K_RETURN;
        break;
      case SDLK_ESCAPE:
        out = K_ESCAPE;
        break;
      case SDLK_BACKSPACE:
        out = K_BACKSPACE;
        break;
      case SDLK_TAB:
        out = K_TAB;
        break;
      case SDLK_PAGEUP:
        out = K_PAGEUP;
        break;
      case SDLK_PAGEDOWN:
        out = K_PAGEDOWN;
        break;
      case SDLK_DELETE:
        out = K_DELETE;
        break;
      case SDLK_RIGHT:
        out = K_RIGHT;
        break;
      case SDLK_LEFT:
        out = K_LEFT;
        break;
      case SDLK_DOWN:
        out = K_DOWN;
        break;
      case SDLK_UP:
        out = K_UP;
        break;
      case SDLK_LCTRL:
        out = K_LCTRL;
        break;
      case SDLK_LSHIFT:
        out = K_LSHIFT;
        break;
      case SDLK_LALT:
        out = K_LALT;
        break;
      case SDLK_LGUI:
        out = K_LGUI;
        break;
      case SDLK_RCTRL:
        out = K_RCTRL;
        break;
      case SDLK_RSHIFT:
        out = K_RSHIFT;
        break;
      case SDLK_RALT:
        out = K_RALT;
        break;
      case SDLK_RGUI:
        out = K_RGUI;
        break;
      case SDLK_F1:
        out = K_F1;
        break;
      case SDLK_F2:
        out = K_F2;
        break;
      case SDLK_F3:
        out = K_F3;
        break;
      case SDLK_F4:
        out = K_F4;
        break;
      case SDLK_F5:
        out = K_F5;
        break;
      case SDLK_F6:
        out = K_F6;
        break;
      case SDLK_F7:
        out = K_F7;
        break;
      case SDLK_F8:
        out = K_F8;
        break;
      case SDLK_F9:
        out = K_F9;
        break;
      case SDLK_F10:
        out = K_F10;
        break;
      case SDLK_F11:
        out = K_F11;
        break;
      case SDLK_F12:
        out = K_F12;
        break;
      case SDLK_F13:
        out = K_F13;
        break;
      case SDLK_F14:
        out = K_F14;
        break;
      case SDLK_F15:
        out = K_F15;
        break;
      case SDLK_F16:
        out = K_F16;
        break;
      case SDLK_F17:
        out = K_F17;
        break;
      case SDLK_F18:
        out = K_F18;
        break;
      case SDLK_F19:
        out = K_F19;
        break;
      case SDLK_F20:
        out = K_F20;
        break;
      case SDLK_F21:
        out = K_F21;
        break;
      case SDLK_F22:
        out = K_F22;
        break;
      case SDLK_F23:
        out = K_F23;
        break;
      case SDLK_F24:
        out = K_F24;
        break;
    }
  }

  if (keysym.scancode == SDL_SCANCODE_GRAVE) {
    out = K_CONSOLE;
  }

  return out;
}

static int input_find_controller_port(struct sdl_host *host, int instance_id) {
  for (int port = 0; port < INPUT_MAX_CONTROLLERS; port++) {
    SDL_GameController *ctrl = host->controllers[port];
    SDL_Joystick *joy = SDL_GameControllerGetJoystick(ctrl);

    if (SDL_JoystickInstanceID(joy) == instance_id) {
      return port;
    }
  }

  return -1;
}

static void input_handle_mousemove(struct sdl_host *host, int port, int x,
                                   int y) {
  if (!host->input_mousemove) {
    return;
  }

  host->input_mousemove(host->userdata, port, x, y);
}

static void input_handle_keydown(struct sdl_host *host, int port,
                                 enum keycode key, int16_t value) {
  if (!host->input_keydown) {
    return;
  }

  host->input_keydown(host->userdata, port, key, value);

  /* if the key is mapped to a controller button, send that event as well */
  int button = host->key_map[key];

  if (button) {
    host->input_keydown(host->userdata, port, button, value);
  }
}

static void input_handle_controller_removed(struct sdl_host *host, int port) {
  SDL_GameController *ctrl = host->controllers[port];

  if (!ctrl) {
    return;
  }

  const char *name = SDL_GameControllerName(ctrl);
  LOG_INFO("controller '%s' removed from port %d", name, port);

  SDL_GameControllerClose(ctrl);
  host->controllers[port] = NULL;
}

static void input_handle_controller_added(struct sdl_host *host,
                                          int device_id) {
  /* find the next open controller port */
  int port;
  for (port = 0; port < INPUT_MAX_CONTROLLERS; port++) {
    if (!host->controllers[port]) {
      break;
    }
  }
  if (port >= INPUT_MAX_CONTROLLERS) {
    LOG_WARNING("no open ports to bind controller to");
    return;
  }

  SDL_GameController *ctrl = SDL_GameControllerOpen(device_id);
  host->controllers[port] = ctrl;

  LOG_INFO("controller '%s' added on port %d", SDL_GameControllerName(ctrl),
           port);
}

static void input_shutdown(struct sdl_host *host) {
  for (int i = 0; i < INPUT_MAX_CONTROLLERS; i++) {
    input_handle_controller_removed(host, i);
  }
}

static int input_init(struct sdl_host *host) {
  /* development key map */
  host->key_map[K_SPACE] = K_CONT_START;
  host->key_map['k'] = K_CONT_A;
  host->key_map['l'] = K_CONT_B;
  host->key_map['j'] = K_CONT_X;
  host->key_map['i'] = K_CONT_Y;
  host->key_map['w'] = K_CONT_DPAD_UP;
  host->key_map['s'] = K_CONT_DPAD_DOWN;
  host->key_map['a'] = K_CONT_DPAD_LEFT;
  host->key_map['d'] = K_CONT_DPAD_RIGHT;
  host->key_map['o'] = K_CONT_LTRIG;
  host->key_map['p'] = K_CONT_RTRIG;

  /* SDL won't push events for joysticks which are already connected at init */
  int num_joysticks = SDL_NumJoysticks();

  for (int device_id = 0; device_id < num_joysticks; device_id++) {
    if (!SDL_IsGameController(device_id)) {
      continue;
    }

    input_handle_controller_added(host, device_id);
  }

  return 1;
}

void input_poll(struct host *base) {
  struct sdl_host *host = (struct sdl_host *)base;

  host_poll_events(host);
}

static void host_swap_window(struct sdl_host *host) {
  SDL_GL_SwapWindow(g_host->win);
}

static void host_poll_events(struct sdl_host *host) {
  SDL_Event ev;

  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_KEYDOWN: {
        enum keycode keycode = translate_sdl_key(ev.key.keysym);

        if (keycode != K_UNKNOWN) {
          input_handle_keydown(host, 0, keycode, KEY_DOWN);
        }
      } break;

      case SDL_KEYUP: {
        enum keycode keycode = translate_sdl_key(ev.key.keysym);

        if (keycode != K_UNKNOWN) {
          input_handle_keydown(host, 0, keycode, KEY_UP);
        }
      } break;

      case SDL_MOUSEBUTTONDOWN:
      case SDL_MOUSEBUTTONUP: {
        enum keycode keycode;

        switch (ev.button.button) {
          case SDL_BUTTON_LEFT:
            keycode = K_MOUSE1;
            break;
          case SDL_BUTTON_RIGHT:
            keycode = K_MOUSE2;
            break;
          case SDL_BUTTON_MIDDLE:
            keycode = K_MOUSE3;
            break;
          case SDL_BUTTON_X1:
            keycode = K_MOUSE4;
            break;
          case SDL_BUTTON_X2:
            keycode = K_MOUSE5;
            break;
          default:
            keycode = K_UNKNOWN;
            break;
        }

        if (keycode != K_UNKNOWN) {
          int16_t value = ev.type == SDL_MOUSEBUTTONDOWN ? KEY_DOWN : KEY_UP;
          input_handle_keydown(host, 0, keycode, value);
        }
      } break;

      case SDL_MOUSEWHEEL:
        if (ev.wheel.y > 0) {
          input_handle_keydown(host, 0, K_MWHEELUP, KEY_DOWN);
          input_handle_keydown(host, 0, K_MWHEELUP, KEY_UP);
        } else {
          input_handle_keydown(host, 0, K_MWHEELDOWN, KEY_DOWN);
          input_handle_keydown(host, 0, K_MWHEELDOWN, KEY_UP);
        }
        break;

      case SDL_MOUSEMOTION:
        input_handle_mousemove(host, 0, ev.motion.x, ev.motion.y);
        break;

      case SDL_CONTROLLERDEVICEADDED: {
        input_handle_controller_added(host, ev.cdevice.which);
      } break;

      case SDL_CONTROLLERDEVICEREMOVED: {
        int port = input_find_controller_port(host, ev.cdevice.which);

        if (port != -1) {
          input_handle_controller_removed(host, port);
        }
      } break;

      case SDL_CONTROLLERAXISMOTION: {
        int port = input_find_controller_port(host, ev.caxis.which);
        enum keycode key = K_UNKNOWN;
        int16_t value = ev.caxis.value;

        switch (ev.caxis.axis) {
          case SDL_CONTROLLER_AXIS_LEFTX:
            key = K_CONT_JOYX;
            break;
          case SDL_CONTROLLER_AXIS_LEFTY:
            key = K_CONT_JOYY;
            break;
          case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            key = K_CONT_LTRIG;
            break;
          case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            key = K_CONT_RTRIG;
            break;
        }

        if (port != -1 && key != K_UNKNOWN) {
          input_handle_keydown(host, port, key, value);
        }
      } break;

      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int port = input_find_controller_port(host, ev.cbutton.which);
        enum keycode key = K_UNKNOWN;
        int16_t value = ev.type == SDL_CONTROLLERBUTTONDOWN ? KEY_DOWN : KEY_UP;

        switch (ev.cbutton.button) {
          case SDL_CONTROLLER_BUTTON_A:
            key = K_CONT_A;
            break;
          case SDL_CONTROLLER_BUTTON_B:
            key = K_CONT_B;
            break;
          case SDL_CONTROLLER_BUTTON_X:
            key = K_CONT_X;
            break;
          case SDL_CONTROLLER_BUTTON_Y:
            key = K_CONT_Y;
            break;
          case SDL_CONTROLLER_BUTTON_START:
            key = K_CONT_START;
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_UP:
            key = K_CONT_DPAD_UP;
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            key = K_CONT_DPAD_DOWN;
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            key = K_CONT_DPAD_LEFT;
            break;
          case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            key = K_CONT_DPAD_RIGHT;
            break;
        }

        if (port != -1 && key != K_UNKNOWN) {
          input_handle_keydown(host, port, key, value);
        }
      } break;

      case SDL_WINDOWEVENT:
        switch (ev.window.event) {
          case SDL_WINDOWEVENT_RESIZED: {
            host->video_width = ev.window.data1;
            host->video_height = ev.window.data2;
            video_resized(host);
          } break;
        }
        break;

      case SDL_QUIT:
        host->closed = 1;
        break;
    }
  }
}

void host_destroy(struct sdl_host *host) {
  input_shutdown(host);

  video_shutdown(host);

  audio_shutdown(host);

  if (host->win) {
    SDL_DestroyWindow(host->win);
  }

  SDL_Quit();

  free(host);
}

struct sdl_host *host_create() {
  struct sdl_host *host = calloc(1, sizeof(struct sdl_host));

  /* init sdl and create window */
  int res = SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
  CHECK_GE(res, 0, "host_create sdl initialization failed: %s", SDL_GetError());

  host->win = SDL_CreateWindow("redream", SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, VIDEO_DEFAULT_WIDTH,
                               VIDEO_DEFAULT_HEIGHT,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  CHECK_NOTNULL(host->win, "host_create window creation failed: %s",
                SDL_GetError());

  /* immediately poll window size for platforms like Android where the window
     starts fullscreen, ignoring the default width and height */
  SDL_GetWindowSize(host->win, &host->video_width, &host->video_height);

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

int main(int argc, char **argv) {
/* set application directory */
#if PLATFORM_ANDROID
  const char *appdir = SDL_AndroidGetExternalStoragePath();
  fs_set_appdir(appdir);
#else
  char userdir[PATH_MAX];
  int r = fs_userdir(userdir, sizeof(userdir));
  CHECK(r);

  char appdir[PATH_MAX];
  snprintf(appdir, sizeof(appdir), "%s" PATH_SEPARATOR ".redream", userdir);
  fs_set_appdir(appdir);
#endif

  /* load base options from config */
  char config[PATH_MAX] = {0};
  snprintf(config, sizeof(config), "%s" PATH_SEPARATOR "config", appdir);
  options_read(config);

  /* override options from the command line */
  if (!options_parse(&argc, &argv)) {
    return EXIT_FAILURE;
  }

  /* init host audio, video and input systems */
  g_host = host_create();
  if (!g_host) {
    return EXIT_FAILURE;
  }

  const char *load = argc > 1 ? argv[1] : NULL;

  if (load && strstr(load, ".trace")) {
    struct tracer *tracer = tracer_create((struct host *)g_host);

    if (tracer_load(tracer, load)) {
      while (!g_host->closed) {
        host_poll_events(g_host);

        tracer_render_frame(tracer);

        host_swap_window(g_host);
      }
    }

    tracer_destroy(tracer);
  } else {
    struct emu *emu = emu_create((struct host *)g_host);

    /* tell the emulator a valid video context is available */
    video_context_reset(g_host);

    if (emu_load_game(emu, load)) {
      while (!g_host->closed) {
        /* even though the emulator itself will poll for events when updating
           controller input, the main loop needs to also poll to ensure the
           close event is received */
        host_poll_events(g_host);

        /* only step the emulator if the available audio is running low. this
           syncs the emulation speed with the host audio clock. note however,
           if audio is disabled, the emulator will run unthrottled */
        if (!audio_buffer_low(g_host)) {
          continue;
        }

        emu_render_frame(emu);

        host_swap_window(g_host);
      }
    }

    video_context_destroyed(g_host);

    emu_destroy(emu);
  }

  host_destroy(g_host);

  /* persist options for next run */
  options_write(config);

  return EXIT_SUCCESS;
}
