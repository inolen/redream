#ifndef OPTIONS_H

#include "core/option.h"

struct button_map {
  const char *desc;
  int *key;
  int *dirty;
};

extern const int LATENCY_PRESETS[];
extern const int NUM_LATENCY_PRESETS;

extern const char *ASPECT_RATIOS[];
extern const int NUM_ASPECT_RATIOS;

extern struct button_map BUTTONS[];
extern const int NUM_BUTTONS;

/* host */
DECLARE_OPTION_INT(audio);
DECLARE_OPTION_INT(latency);
DECLARE_OPTION_INT(fullscreen);
DECLARE_OPTION_INT(key_a);
DECLARE_OPTION_INT(key_b);
DECLARE_OPTION_INT(key_x);
DECLARE_OPTION_INT(key_y);
DECLARE_OPTION_INT(key_start);
DECLARE_OPTION_INT(key_dup);
DECLARE_OPTION_INT(key_ddown);
DECLARE_OPTION_INT(key_dleft);
DECLARE_OPTION_INT(key_dright);
DECLARE_OPTION_INT(key_joyx);
DECLARE_OPTION_INT(key_joyy);
DECLARE_OPTION_INT(key_ltrig);
DECLARE_OPTION_INT(key_rtrig);

/* emulator */
DECLARE_OPTION_INT(debug);
DECLARE_OPTION_STRING(aspect);

/* bios */
DECLARE_OPTION_STRING(region);
DECLARE_OPTION_STRING(language);
DECLARE_OPTION_STRING(broadcast);

/* jit */
DECLARE_OPTION_INT(perf);

/* ui */
DECLARE_OPTION_STRING(gamedir);

#endif
