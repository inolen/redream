#include <glad/glad.h>
#include <libretro.h>
#include <stdio.h>
#include <stdlib.h>
#include "core/core.h"
#include "core/filesystem.h"
#include "emulator.h"
#include "guest/aica/aica.h"
#include "host/host.h"
#include "options.h"
#include "render/render_backend.h"

#define AUDIO_FREQ AICA_SAMPLE_FREQ
#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480

static struct retro_hw_render_callback hw_render;
static retro_environment_t env_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

/*
 * libretro host implementation
 */

/* clang-format off */
#define NUM_CONTROLLER_DESC          (ARRAY_SIZE(controller_desc)-1)

#define CONTROLLER_DESC(port)                                                                                 \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_A,     "B" },           \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_B,     "A" },           \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_START, "Start" },       \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },    \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },  \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },  \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" }, \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_X,     "Y" },           \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_Y,     "X" },           \
  { port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X,     "Analog X" },    \
  { port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y,     "Analog Y" },    \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_L2,    "L" },           \
  { port, RETRO_DEVICE_JOYPAD, 0,                              RETRO_DEVICE_ID_JOYPAD_R2,    "R" }

#define CONTROLLER_BUTTONS(port) \
  K_CONT_B,                      \
  K_CONT_A,                      \
  K_CONT_START,                  \
  K_CONT_DPAD_UP,                \
  K_CONT_DPAD_DOWN,              \
  K_CONT_DPAD_LEFT,              \
  K_CONT_DPAD_RIGHT,             \
  K_CONT_Y,                      \
  K_CONT_X,                      \
  K_CONT_JOYX,                   \
  K_CONT_JOYY,                   \
  K_CONT_LTRIG,                  \
  K_CONT_RTRIG

static struct retro_input_descriptor controller_desc[] = {
  CONTROLLER_DESC(0),
  CONTROLLER_DESC(1),
  CONTROLLER_DESC(2),
  CONTROLLER_DESC(3),
  { 0 },
};

static int controller_buttons[] = {
  CONTROLLER_BUTTONS(0),
  CONTROLLER_BUTTONS(1),
  CONTROLLER_BUTTONS(2),
  CONTROLLER_BUTTONS(3),
};
/* clang-format on */

struct host {
  struct emu *emu;

  struct {
    struct render_backend *r;
  } video;

  struct {
    int16_t state[NUM_CONTROLLER_DESC];
  } input;
};

struct host *g_host;

/*
 * audio
 */
void audio_push(struct host *host, const int16_t *data, int frames) {
  audio_batch_cb(data, frames);
}

/*
 * input
 */
static void input_poll(struct host *host) {
  input_poll_cb();

  /* send updates for any inputs that've changed */
  for (int i = 0; i < NUM_CONTROLLER_DESC; i++) {
    struct retro_input_descriptor *desc = &controller_desc[i];
    int16_t value =
        input_state_cb(desc->port, desc->device, desc->index, desc->id);

    /* retroarch's API provides a binary [0, 1] value for the triggers. map from
       this to [0, INT16_MAX] as our host layer expects */
    if (desc->id == RETRO_DEVICE_ID_JOYPAD_L2 ||
        desc->id == RETRO_DEVICE_ID_JOYPAD_R2) {
      value = value ? INT16_MAX : 0;
    }

    if (host->input.state[i] == value) {
      continue;
    }

    int button = controller_buttons[i];

    if (host->emu) {
      emu_keydown(host->emu, desc->port, button, value);
    }

    host->input.state[i] = value;
  }
}

/*
 * internal
 */
static void video_context_destroyed() {
  if (g_host->emu) {
    emu_vid_destroyed(g_host->emu);
  }

  CHECK_NOTNULL(g_host->video.r);
  r_destroy(g_host->video.r);
  g_host->video.r = NULL;
}

static void video_context_reset() {
  /* link in gl functions at runtime */
  int res = gladLoadGLLoader((GLADloadproc)hw_render.get_proc_address);
  CHECK_EQ(res, 1, "GL initialization failed");

  CHECK(!g_host->video.r);
  g_host->video.r = r_create(VIDEO_WIDTH, VIDEO_HEIGHT);

  if (g_host->emu) {
    emu_vid_created(g_host->emu, g_host->video.r);
  }
}

static int host_init(struct host *host) {
  /* let retroarch know about our controller mappings */
  env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, controller_desc);

  /* request an initial OpenGL context */
  hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
  hw_render.version_major = 3;
  hw_render.version_minor = 3;
  hw_render.context_reset = &video_context_reset;
  hw_render.context_destroy = &video_context_destroyed;
  hw_render.depth = true;
  hw_render.bottom_left_origin = true;

  bool ret = env_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render);
  if (!ret) {
    LOG_WARNING("host_init failed to initialize hardware renderer");
    return 0;
  }

  return 1;
}

static void host_destroy(struct host *host) {
  free(host);
}

struct host *host_create() {
  struct host *host = calloc(1, sizeof(struct host));
  return host;
}

/*
 * libretro core implementation
 */
void retro_init() {
  /* set application directory */
  const char *sysdir = NULL;
  if (env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sysdir)) {
    char appdir[PATH_MAX];
    snprintf(appdir, sizeof(appdir), "%s" PATH_SEPARATOR "dc", sysdir);
    fs_set_appdir(appdir);
  }

  /* load base options from config */
  const char *appdir = fs_appdir();
  char config[PATH_MAX] = {0};
  snprintf(config, sizeof(config), "%s" PATH_SEPARATOR "config", appdir);
  options_read(config);
}

void retro_deinit() {}

unsigned retro_api_version() {
  return RETRO_API_VERSION;
}

void retro_get_system_info(struct retro_system_info *info) {
  info->library_name = "redream";
  info->library_version = "0.0";
  info->valid_extensions = "cdi|chd|gdi";
  info->need_fullpath = true;
  info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
  info->geometry.base_width = VIDEO_WIDTH;
  info->geometry.base_height = VIDEO_HEIGHT;
  info->geometry.max_width = VIDEO_WIDTH;
  info->geometry.max_height = VIDEO_HEIGHT;
  info->geometry.aspect_ratio = (float)VIDEO_WIDTH / (float)VIDEO_HEIGHT;
  info->timing.fps = 60;
  info->timing.sample_rate = AUDIO_FREQ;
}

void retro_set_environment(retro_environment_t env) {
  env_cb = env;
}

void retro_set_video_refresh(retro_video_refresh_t video) {
  video_cb = video;
}

void retro_set_audio_sample(retro_audio_sample_t audio) {
  audio_cb = audio;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t audio_batch) {
  audio_batch_cb = audio_batch;
}

void retro_set_input_poll(retro_input_poll_t input_poll) {
  input_poll_cb = input_poll;
}

void retro_set_input_state(retro_input_state_t input_state) {
  input_state_cb = input_state;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {}

void retro_reset() {}

void retro_run() {
  input_poll(g_host);

  /* bind the framebuffer provided by retroarch before calling into the
     emulator */
  uintptr_t fb = hw_render.get_current_framebuffer();
  glBindFramebuffer(GL_FRAMEBUFFER, fb);

  emu_render_frame(g_host->emu);

  /* call back into retroarch, letting it know a frame has been rendered */
  video_cb(RETRO_HW_FRAME_BUFFER_VALID, VIDEO_WIDTH, VIDEO_HEIGHT, 0);
}

size_t retro_serialize_size() {
  return 0;
}

bool retro_serialize(void *data, size_t size) {
  return false;
}

bool retro_unserialize(const void *data, size_t size) {
  return false;
}

void retro_cheat_reset() {}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {}

bool retro_load_game(const struct retro_game_info *info) {
  g_host = host_create();
  g_host->emu = emu_create(g_host);

  if (!host_init(g_host)) {
    emu_destroy(g_host->emu);
    g_host->emu = NULL;

    host_destroy(g_host);
    g_host = NULL;

    return false;
  }

  return emu_load(g_host->emu, info->path);
}

bool retro_load_game_special(unsigned game_type,
                             const struct retro_game_info *info,
                             size_t num_info) {
  return false;
}

void retro_unload_game() {
  if (g_host->emu) {
    emu_destroy(g_host->emu);
    g_host->emu = NULL;
  }

  if (g_host) {
    host_destroy(g_host);
    g_host = NULL;
  }
}

unsigned retro_get_region() {
  return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned id) {
  return NULL;
}

size_t retro_get_memory_size(unsigned id) {
  return 0;
}
