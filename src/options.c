#include "options.h"
#include "core/core.h"
#include "host/keycode.h"

/* default deadzone taken from: https://forums.libsdl.org/viewtopic.php?p=39985
   this thread is specifically talking about 360 controllers, but other sources
   also have a default of around this */
#define DEFAULT_DEADZONE 4096

/* clang-format off */
const char *BROADCASTS[] = {
  "ntsc",
  "pal",
  "pal_m",
  "pal_n",
};
const int NUM_BROADCASTS = ARRAY_SIZE(BROADCASTS);

const char *REGIONS[] = {
  "japan",
  "usa",
  "europe",
};
const int NUM_REGIONS = ARRAY_SIZE(REGIONS);

const char *LANGUAGES[] = {
  "japanese",
  "english",
  "german",
  "french",
  "spanish",
  "italian",
};
const int NUM_LANGUAGES = ARRAY_SIZE(LANGUAGES);

const char *TIMESYNCS[] = {
  "audio and video",
  "audio only",
  "video only",
  "none",
};
const int NUM_TIMESYNCS = ARRAY_SIZE(TIMESYNCS);

const char *ASPECT_RATIOS[] = {
  "stretch",
  "16:9",
  "4:3",
};
const int NUM_ASPECT_RATIOS = ARRAY_SIZE(ASPECT_RATIOS);

struct button_map BUTTONS[] = {
  {"B button",         K_CONT_B,          DIR_NONE, &OPTION_key_b,        &OPTION_key_b_dirty},
  {"A button",         K_CONT_A,          DIR_NONE, &OPTION_key_a,        &OPTION_key_a_dirty},
  {"Start button",     K_CONT_START,      DIR_NONE, &OPTION_key_start,    &OPTION_key_start_dirty},
  {"DPAD Up",          K_CONT_DPAD_UP,    DIR_NONE, &OPTION_key_dup,      &OPTION_key_dup_dirty},
  {"DPAD Down",        K_CONT_DPAD_DOWN,  DIR_NONE, &OPTION_key_ddown,    &OPTION_key_ddown_dirty},
  {"DPAD Left",        K_CONT_DPAD_LEFT,  DIR_NONE, &OPTION_key_dleft,    &OPTION_key_dleft_dirty},
  {"DPAD Right",       K_CONT_DPAD_RIGHT, DIR_NONE, &OPTION_key_dright,   &OPTION_key_dright_dirty},
  {"Y button",         K_CONT_Y,          DIR_NONE, &OPTION_key_y,        &OPTION_key_y_dirty},
  {"X button",         K_CONT_X,          DIR_NONE, &OPTION_key_x,        &OPTION_key_x_dirty},
  {"Joystick -X axis", K_CONT_JOYX,       DIR_NEG,  &OPTION_key_joyx_neg, &OPTION_key_joyx_neg_dirty},
  {"Joystick +X axis", K_CONT_JOYX,       DIR_POS,  &OPTION_key_joyx_pos, &OPTION_key_joyx_pos_dirty},
  {"Joystick -Y axis", K_CONT_JOYY,       DIR_NEG,  &OPTION_key_joyy_neg, &OPTION_key_joyy_neg_dirty},
  {"Joystick +Y axis", K_CONT_JOYY,       DIR_POS,  &OPTION_key_joyy_pos, &OPTION_key_joyy_pos_dirty},
  {"Left trigger",     K_CONT_LTRIG,      DIR_POS,  &OPTION_key_ltrig,    &OPTION_key_ltrig_dirty},
  {"Right trigger",    K_CONT_RTRIG,      DIR_POS,  &OPTION_key_rtrig,    &OPTION_key_rtrig_dirty},
};
const int NUM_BUTTONS = ARRAY_SIZE(BUTTONS);

int *DEADZONES[] = {
  &OPTION_deadzone_0,
  &OPTION_deadzone_1,
  &OPTION_deadzone_2,
  &OPTION_deadzone_3
};

/* host */
DEFINE_OPTION_INT(bios,                    0,                 "Boot to bios");
DEFINE_PERSISTENT_OPTION_STRING(sync,      "audio and video", "Time sync");
DEFINE_PERSISTENT_OPTION_INT(fullscreen,   0,                 "Start window fullscreen");
DEFINE_PERSISTENT_OPTION_INT(key_a,        'l',               "A button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_b,        'p',               "B button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_x,        'k',               "X button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_y,        'o',               "Y button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_start,    ' ',               "Start button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_dup,      't',               "DPAD Up mapping");
DEFINE_PERSISTENT_OPTION_INT(key_ddown,    'g',               "DPAD Down mapping");
DEFINE_PERSISTENT_OPTION_INT(key_dleft,    'f',               "DPAD Left mapping");
DEFINE_PERSISTENT_OPTION_INT(key_dright,   'h',               "DPAD Right mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyx_neg, 'a',               "Joystick -X axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyx_pos, 'd',               "Joystick +X axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyy_neg, 'w',               "Joystick -Y axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyy_pos, 's',               "Joystick +Y axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_ltrig,    '[',               "Left trigger mapping");
DEFINE_PERSISTENT_OPTION_INT(key_rtrig,    ']',               "Right trigger mapping");

DEFINE_PERSISTENT_OPTION_INT(deadzone_0,   DEFAULT_DEADZONE,  "Deadzone for controller on port 0");
DEFINE_PERSISTENT_OPTION_INT(deadzone_1,   DEFAULT_DEADZONE,  "Deadzone for controller on port 1");
DEFINE_PERSISTENT_OPTION_INT(deadzone_2,   DEFAULT_DEADZONE,  "Deadzone for controller on port 2");
DEFINE_PERSISTENT_OPTION_INT(deadzone_3,   DEFAULT_DEADZONE,  "Deadzone for controller on port 3");

/* emulator */
DEFINE_PERSISTENT_OPTION_STRING(aspect,    "4:3",             "Video aspect ratio");

/* bios */
DEFINE_PERSISTENT_OPTION_STRING(region,    "usa",             "System region");
DEFINE_PERSISTENT_OPTION_STRING(language,  "english",         "System language");
DEFINE_PERSISTENT_OPTION_STRING(broadcast, "ntsc",            "System broadcast mode");

/* jit */
DEFINE_OPTION_INT(perf,                    0,                 "Create maps for compiled code for use with perf");

/* ui */
DEFINE_PERSISTENT_OPTION_STRING(gamedir,   "",                "Directories to scan for games");

/* clang-format on */

int audio_sync_enabled() {
  const char *ptr = OPTION_sync;
  while (*ptr) {
    if (*(ptr++) == 'a') {
      return 1;
    }
  }
  return 0;
}

int video_sync_enabled() {
  const char *ptr = OPTION_sync;
  while (*ptr) {
    if (*(ptr++) == 'v') {
      return 1;
    }
  }
  return 0;
}
