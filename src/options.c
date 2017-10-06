#include "options.h"
#include "core/core.h"
#include "host/keycode.h"

const int LATENCY_PRESETS[] = {50, 75, 100, 125, 150};
const int NUM_LATENCY_PRESETS = ARRAY_SIZE(LATENCY_PRESETS);

const char *ASPECT_RATIOS[] = {
    "stretch", "16:9", "4:3",
};
const int NUM_ASPECT_RATIOS = ARRAY_SIZE(ASPECT_RATIOS);

struct button_map BUTTONS[] = {
    {NULL, NULL, NULL}, /* K_CONT_C */
    {"B button", &OPTION_key_b, &OPTION_key_b_dirty},
    {"A button", &OPTION_key_a, &OPTION_key_a_dirty},
    {"Start button", &OPTION_key_start, &OPTION_key_start_dirty},
    {"DPAD Up", &OPTION_key_dup, &OPTION_key_dup_dirty},
    {"DPAD Down", &OPTION_key_ddown, &OPTION_key_ddown_dirty},
    {"DPAD Left", &OPTION_key_dleft, &OPTION_key_dleft_dirty},
    {"DPAD Right", &OPTION_key_dright, &OPTION_key_dright_dirty},
    {NULL, NULL, NULL}, /* K_CONT_Z */
    {"Y button", &OPTION_key_y, &OPTION_key_y_dirty},
    {"X button", &OPTION_key_x, &OPTION_key_x_dirty},
    {NULL, NULL, NULL}, /* K_CONT_D */
    {NULL, NULL, NULL}, /* K_CONT_DPAD2_UP */
    {NULL, NULL, NULL}, /* K_CONT_DPAD2_DOWN */
    {NULL, NULL, NULL}, /* K_CONT_DPAD2_LEFT */
    {NULL, NULL, NULL}, /* K_CONT_DPAD2_RIGHT */
    {"Joystick X axis", &OPTION_key_joyx, &OPTION_key_joyx_dirty},
    {"Joystick Y axis", &OPTION_key_joyy, &OPTION_key_joyy_dirty},
    {"Left trigger", &OPTION_key_ltrig, &OPTION_key_ltrig_dirty},
    {"Right trigger", &OPTION_key_rtrig, &OPTION_key_rtrig_dirty},
};
const int NUM_BUTTONS = ARRAY_SIZE(BUTTONS);

/* host */
DEFINE_OPTION_INT(audio, 1, "Enable audio");
DEFINE_PERSISTENT_OPTION_INT(latency, 50, "Preferred audio latency in ms");
DEFINE_PERSISTENT_OPTION_INT(fullscreen, 0, "Start window fullscreen");
DEFINE_PERSISTENT_OPTION_INT(key_a, 'k', "A button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_b, 'l', "B button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_x, 'j', "X button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_y, 'i', "Y button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_start, K_SPACE, "Start button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_dup, 'w', "DPAD Up mapping");
DEFINE_PERSISTENT_OPTION_INT(key_ddown, 's', "DPAD Down mapping");
DEFINE_PERSISTENT_OPTION_INT(key_dleft, 'a', "DPAD Left mapping");
DEFINE_PERSISTENT_OPTION_INT(key_dright, 'd', "DPAD Right mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyx, 0, "Joystick X axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyy, 0, "Joystick Y axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_ltrig, 'o', "Left trigger mapping");
DEFINE_PERSISTENT_OPTION_INT(key_rtrig, 'p', "Right trigger mapping");

/* emulator */
DEFINE_PERSISTENT_OPTION_STRING(aspect, "stretch", "Video aspect ratio");

/* bios */
DEFINE_PERSISTENT_OPTION_STRING(region, "usa", "System region");
DEFINE_PERSISTENT_OPTION_STRING(language, "english", "System language");
DEFINE_PERSISTENT_OPTION_STRING(broadcast, "ntsc", "System broadcast mode");

/* jit */
DEFINE_OPTION_INT(perf, 0, "Create maps for compiled code for use with perf");

/* ui */
DEFINE_PERSISTENT_OPTION_STRING(gamedir, "", "Directories to scan for games");
