#ifndef OPTIONS_H

#include "core/option.h"

/* host */
DECLARE_OPTION_INT(audio);
DECLARE_OPTION_INT(latency);
DECLARE_OPTION_INT(fullscreen);

/* emulator */
DECLARE_OPTION_INT(debug);
DECLARE_OPTION_STRING(aspect);

/* bios */
DECLARE_OPTION_STRING(region);
DECLARE_OPTION_STRING(language);
DECLARE_OPTION_STRING(broadcast);

/* jit */
DECLARE_OPTION_INT(perf);

#endif
