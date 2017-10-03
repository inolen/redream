#include "options.h"

/* host */
DEFINE_OPTION_INT(audio, 1, "Enable audio");
DEFINE_OPTION_INT(latency, 50, "Preferred audio latency in ms");
DEFINE_PERSISTENT_OPTION_INT(fullscreen, 0, "Start window fullscreen");

/* emulator */
DEFINE_PERSISTENT_OPTION_INT(debug, 1, "Show debug menu");
DEFINE_PERSISTENT_OPTION_STRING(aspect, "stretch", "Video aspect ratio");

/* bios */
DEFINE_PERSISTENT_OPTION_STRING(region, "usa", "System region");
DEFINE_PERSISTENT_OPTION_STRING(language, "english", "System language");
DEFINE_PERSISTENT_OPTION_STRING(broadcast, "ntsc", "System broadcast mode");

/* jit */
DEFINE_OPTION_INT(perf, 0, "Create maps for compiled code for use with perf");
