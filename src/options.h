#ifndef OPTIONS_H

#include "core/option.h"

enum {
  DIR_NONE,
  DIR_NEG,
  DIR_POS,
};

struct button_map {
  const char *desc;
  int btn;
  int dir;
  int *key;
  int *dirty;
};

extern const char *BROADCASTS[];
extern const int NUM_BROADCASTS;

extern const char *LANGUAGES[];
extern const int NUM_LANGUAGES;

extern const char *REGIONS[];
extern const int NUM_REGIONS;

extern const char *TIMESYNCS[];
extern const int NUM_TIMESYNCS;

extern const char *ASPECT_RATIOS[];
extern const int NUM_ASPECT_RATIOS;

extern struct button_map BUTTONS[];
extern const int NUM_BUTTONS;

extern int *DEADZONES[];

/* host */
DECLARE_OPTION_STRING(sync);
DECLARE_OPTION_INT(bios);
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
DECLARE_OPTION_INT(key_joyx_neg);
DECLARE_OPTION_INT(key_joyx_pos);
DECLARE_OPTION_INT(key_joyy_neg);
DECLARE_OPTION_INT(key_joyy_pos);
DECLARE_OPTION_INT(key_ltrig);
DECLARE_OPTION_INT(key_rtrig);
DECLARE_OPTION_INT(deadzone_0);
DECLARE_OPTION_INT(deadzone_1);
DECLARE_OPTION_INT(deadzone_2);
DECLARE_OPTION_INT(deadzone_3);

/* emulator */
DECLARE_OPTION_STRING(aspect);

/* bios */
DECLARE_OPTION_STRING(region);
DECLARE_OPTION_STRING(language);
DECLARE_OPTION_STRING(broadcast);

/* jit */
DECLARE_OPTION_INT(perf);

/* ui */
DECLARE_OPTION_STRING(gamedir);

int audio_sync_enabled();
int video_sync_enabled();

#endif
