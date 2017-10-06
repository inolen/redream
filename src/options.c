#include "options.h"
#include "core/core.h"
#include "host/keycode.h"

const int LATENCY_PRESETS[] = {45, 90, 180};
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
    {"Joystick -X axis", &OPTION_key_joyx_neg, &OPTION_key_joyx_neg_dirty},
    {"Joystick +X axis", &OPTION_key_joyx_pos, &OPTION_key_joyx_pos_dirty},
    {"Joystick -Y axis", &OPTION_key_joyy_neg, &OPTION_key_joyy_neg_dirty},
    {"Joystick +Y axis", &OPTION_key_joyy_pos, &OPTION_key_joyy_pos_dirty},
    {"Left trigger", &OPTION_key_ltrig, &OPTION_key_ltrig_dirty},
    {"Right trigger", &OPTION_key_rtrig, &OPTION_key_rtrig_dirty},
};
const int NUM_BUTTONS = ARRAY_SIZE(BUTTONS);

/* host */
DEFINE_OPTION_INT(bios, 0, "Boot to bios");
DEFINE_OPTION_INT(audio, 1, "Enable audio");
DEFINE_PERSISTENT_OPTION_INT(latency, 50, "Preferred audio latency in ms");
DEFINE_PERSISTENT_OPTION_INT(fullscreen, 0, "Start window fullscreen");
DEFINE_PERSISTENT_OPTION_INT(key_a, 'l', "A button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_b, 'p', "B button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_x, 'k', "X button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_y, 'o', "Y button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_start, K_SPACE, "Start button mapping");
DEFINE_PERSISTENT_OPTION_INT(key_dup, 't', "DPAD Up mapping");
DEFINE_PERSISTENT_OPTION_INT(key_ddown, 'g', "DPAD Down mapping");
DEFINE_PERSISTENT_OPTION_INT(key_dleft, 'f', "DPAD Left mapping");
DEFINE_PERSISTENT_OPTION_INT(key_dright, 'h', "DPAD Right mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyx_neg, 'a', "Joystick -X axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyx_pos, 'd', "Joystick +X axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyy_neg, 'w', "Joystick -Y axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_joyy_pos, 's', "Joystick +Y axis mapping");
DEFINE_PERSISTENT_OPTION_INT(key_ltrig, '[', "Left trigger mapping");
DEFINE_PERSISTENT_OPTION_INT(key_rtrig, ']', "Right trigger mapping");

/* emulator */
DEFINE_PERSISTENT_OPTION_STRING(aspect, "4:3", "Video aspect ratio");

/* bios */
DEFINE_PERSISTENT_OPTION_STRING(region, "usa", "System region");
DEFINE_PERSISTENT_OPTION_STRING(language, "english", "System language");
DEFINE_PERSISTENT_OPTION_STRING(broadcast, "ntsc", "System broadcast mode");

/* jit */
DEFINE_OPTION_INT(perf, 0, "Create maps for compiled code for use with perf");

/* ui */
DEFINE_PERSISTENT_OPTION_STRING(gamedir, "", "Directories to scan for games");
